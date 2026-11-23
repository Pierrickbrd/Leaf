// The queue, and the rules nobody clicks.
//
// Most of this file is about one sentence of the spec: *pause yields a place, and nothing
// takes it back but its reader*. There is exactly one slot, pausing hands it to the next line
// that was let go, resuming takes it back — whoever held it waits its turn — and a file that
// was paused stays paused until it is asked for again. None of that is visible from a screen —
// it is the model's whole job, and it is why the model is tested here rather than through a
// dialog.

#include "Aimed.h"
#include "Api.h"
#include "Carrier.h"
#include "Cbz.h"
#include "ImportOffers.h"
#include "ImportQueue.h"
#include "ImportTally.h"
#include "Imports.h"
#include "Manifest.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"
#include "Zip.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

QByteArray aReply(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

/// A proposal the server is sure of: one series, the place free, nothing to ask.
QByteArray aReservation(const QByteArray &id, const QByteArray &confidence = "CERTAIN")
{
    const QByteArray candidates = confidence == "UNKNOWN"
        ? QByteArrayLiteral("[]")
        : QByteArrayLiteral(R"([{"seriesId":"ed-1","name":"Death Note"}])");
    return aReply(200,
                  "{\"id\":\"" + id + "\",\"proposal\":{"
                  "\"received\":\"" + id + "\",\"name\":\"Tome.cbz\",\"size\":9,"
                  "\"read\":{},\"confidence\":\"" + confidence + "\","
                  "\"reason\":\"une seule série\",\"candidates\":" + candidates + "}}");
}

} // namespace

class HoldsTheImports : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_folder;
    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Imports *m_imports = nullptr;
    ImportOffers *m_offers = nullptr;
    ImportQueue *m_queue = nullptr;
    ImportTally *m_tally = nullptr;
    int m_opened = 0;

    /// A local file of `bytes` octets. Not an archive: `Cbz` says so, the pre-flight goes
    /// without a sidecar, and that is a case the spec asks for anyway.
    QString aFile(const QString &name, int bytes = 9)
    {
        const QString path = m_folder.filePath(name);
        QFile file(path);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(QByteArray(bytes, 'x'));
        file.close();
        return path;
    }

    /// A real archive at `path` whose own `entry.json` says its number, so that nothing
    /// about it is guessed and no test that is not about the checking step stops on it.
    /// `pageBytes` makes it heavy, for the tests that need a file to cross a chunk or to
    /// take long enough to hash that a count can be seen moving.
    static void aVolume(const QString &path, int number, int pageBytes = 0)
    {
        const QByteArray said = R"({"leaf":1,"number":)" + QByteArray::number(number) + '}';
        QList<Zip::Member> members;
        members << Zip::Member{"001.jpg", QByteArray(qMax(pageBytes, 8), 'x'), false, {}}
                << Zip::Member{"entry.json", said, true, {}};
        QFile file(path);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(Zip::anArchive(members));
    }

    /// A `work.json` that says everything the checking step would otherwise stop on: the
    /// title, the reading direction a series read the wrong way round needs said, and who
    /// wrote and drew it — a series credited to nobody is held back like one with no name.
    static void aDeclaration(const QString &folder, const QByteArray &title)
    {
        QFile declaration(folder + u"/work.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"title":")" + title
                          + R"(","readingDirection":"RIGHT_TO_LEFT","authors":["Toriyama"],"artists":["Toriyama"]})");
    }

    /// What the files of a folder weigh together, since a real archive's size is whatever
    /// its zip structure makes it rather than a number a test chose.
    static qint64 weightOf(const QString &folder)
    {
        qint64 total = 0;
        for (const QFileInfo &one : QDir(folder).entryInfoList({u"*.cbz"_s}, QDir::Files))
            total += one.size();
        return total;
    }

    /// A folder with its declaration and three volumes, one of which the server will say
    /// it already has. Everything about it is said, so it never stops to be checked.
    QString aFolder(const QString &name = u"Koro"_s)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        aDeclaration(at, "Koro Quest");
        for (int number = 1; number <= 3; ++number)
            aVolume(at + u"/Tome %1.cbz"_s.arg(number), number);
        return at;
    }


    /// A folder the way a Komga library leaves one: no `work.json`, and in every archive a
    /// `ComicInfo.xml` that says the series and the number. Nothing about it is guessed, but
    /// nobody said which way it reads — the one missing field that stops the checking step here,
    /// because a series read the wrong way round is not noticed until page forty.
    QString aKomgaFolder(const QString &name = u"Ao Haru Ride"_s, int volumes = 2)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        for (int number = 1; number <= volumes; ++number) {
            QFile file(at + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}, "<ComicInfo><Series>" + name.toUtf8()
                                           + "</Series><Number>" + QByteArray::number(number)
                                           + "</Number><Writer>Yuki</Writer><Penciller>Yuki</Penciller>"
                                             "</ComicInfo>"));
        }
        return at;
    }

    /// Says what a folder is held back for and nobody wrote: the reading direction, and who
    /// wrote and drew a series (`Facts::lacks`). A Komga library says none of the three, and
    /// a test that is about what happens once a card goes has to get it past them first.
    /// Typed as a reader types it, one correction each, so the card is `edited` after it.
    void sayWhatIsLacking(int row)
    {
        const quint64 token = cardAt(row).token;
        const QList<Facts::Sheet> sheets = cardAt(row).road.sheets;
        bool said = false;
        for (const Facts::Sheet &sheet : sheets) {
            for (const Facts::Field &field : sheet.fields) {
                if (!Facts::lacks(sheet, field))
                    continue;
                const QJsonValue value = field.key == u"readingDirection"_s
                                            ? QJsonValue(u"RIGHT_TO_LEFT"_s)
                                            : QJsonValue(QJsonArray{u"Yuki"_s});
                if (field.key == u"readingDirection"_s || field.key == u"authors"_s
                    || field.key == u"artists"_s)
                    said = m_imports->edit(token, sheet.at, field.key, value) || said;
            }
        }
        // Each correction announces the card again, and it is let go only once that is answered.
        if (said)
            QTRY_VERIFY(stageOf(row) != int(Imports::Stage::Asking));
    }

    /// A folder that stops the checking step and holds nothing back: everything it is read
    /// by is said, and its first volume carries an `entry.json` that is not an object — a
    /// file to look at, which a series does not wait for. Its numbers, guessed from the
    /// names, stopped it once; a number read off a name is no longer a question.
    QString aGuessedFolder(const QString &name = u"Ao Haru Ride"_s, int volumes = 2)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        for (int number = 1; number <= volumes; ++number) {
            QFile file(at + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume(number == 1 ? QByteArray("[]") : QByteArray(), "<ComicInfo><Series>" + name.toUtf8()
                                           + "</Series><Writer>Yuki</Writer><Penciller>Yuki"
                                             "</Penciller><Manga>YesAndRightToLeft</Manga>"
                                             "</ComicInfo>"));
        }
        return at;
    }

    /// The JSON body of every request `m_pretend` heard whose first line starts with `line`,
    /// in the order they arrived. Read off the wire rather than off the model, because what
    /// the server receives is the whole point of composing it.
    QList<QJsonObject> bodiesOf(const QByteArray &line) const
    {
        QList<QJsonObject> out;
        const QByteArray &heard = m_pretend->heard;
        for (qsizetype at = heard.indexOf(line); at >= 0; at = heard.indexOf(line, at + 1)) {
            const qsizetype headEnd = heard.indexOf("\r\n\r\n", at);
            if (headEnd < 0)
                break;
            qsizetype length = 0;
            for (const QByteArray &header : heard.mid(at, headEnd - at).split('\n')) {
                if (header.toLower().startsWith("content-length:"))
                    length = header.mid(15).trimmed().toLongLong();
            }
            out << QJsonDocument::fromJson(heard.mid(headEnd + 4, length)).object();
        }
        return out;
    }

    /// The card at `row`, read through the model's own accessor.
    const Card &cardAt(int row) const { return m_imports->cards().at(row); }

    /// The field `key` of the sheet at `at` on the card at `row`.
    Facts::Field fieldOf(int row, const QString &at, const QString &key) const
    {
        for (const Facts::Sheet &sheet : cardAt(row).road.sheets) {
            if (sheet.at != at)
                continue;
            for (const Facts::Field &field : sheet.fields) {
                if (field.key == key)
                    return field;
            }
        }
        return {};
    }

    int rowNamed(const QString &name) const
    {
        for (int row = 0; row < m_imports->count(); ++row) {
            if (nameOf(row) == name)
                return row;
        }
        return -1;
    }

    /// A universe that declares itself, holding two series that declare themselves too.
    ///
    /// The one shape the flat folders above never make: a card with containers under it,
    /// which is what every recursion in this file — the rebase, the flatten, the count of
    /// what would be landed on — exists for.
    QString aUniverse()
    {
        const QString at = m_folder.filePath(u"Terres"_s);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QFile declaration(at + u"/universe.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"name":"Terres d’Arran"})");
        declaration.close();
        for (const QString &series : {u"Elfes"_s, u"Mages"_s}) {
            [&] { QVERIFY(QDir().mkpath(at + u'/' + series)); }();
            aDeclaration(at + u'/' + series, series.toUtf8());
            aVolume(at + u'/' + series + u"/Tome 1.cbz"_s, 1);
        }
        return at;
    }

    /// Volumes `from` to `to` in `folder`, real archives that say nothing about themselves:
    /// what a folder of bare scans holds, so every role in it is read from its shape.
    static void bareVolumes(const QString &folder, int from, int to)
    {
        [&] { QVERIFY(QDir().mkpath(folder)); }();
        for (int number = from; number <= to; ++number) {
            QFile file(folder + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}));
        }
    }

    /// A shelf with nothing said anywhere: a universe of two series, one series
    /// in two editions, and a folder of scans with no title.
    QString aShelfOfUnsaidFolders()
    {
        const QString bd = m_folder.filePath(u"BD"_s);
        bareVolumes(bd + u"/Terres d'Arran/Elfes"_s, 1, 4);
        bareVolumes(bd + u"/Terres d'Arran/Mages"_s, 1, 3);
        bareVolumes(bd + u"/Dragon Ball/Perfect"_s, 1, 3);
        bareVolumes(bd + u"/Dragon Ball/Original"_s, 1, 3);
        bareVolumes(bd + u"/Vrac"_s, 1, 2);
        return bd;
    }

    /// A universe that declares itself, a series in it that does too, and at its root an
    /// archive that says nothing — which the scan files nowhere.
    QString aUniverseWithAnArchiveAtItsRoot()
    {
        const QString at = m_folder.filePath(u"Monde d'Aquilon"_s);
        [&] { QVERIFY(QDir().mkpath(at + u"/Elfes"_s)); }();
        QFile declaration(at + u"/universe.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"name":"Monde d’Aquilon"})");
        declaration.close();
        aDeclaration(at + u"/Elfes"_s, "Elfes");
        aVolume(at + u"/Elfes/Tome 1.cbz"_s, 1);
        QFile scan(at + u"/scan_final.cbz"_s);
        [&] { QVERIFY(scan.open(QIODevice::WriteOnly)); }();
        scan.write(Zip::volume({}));
        return at;
    }

    /// One session per announcement, numbered in the order they come: a shelf announces its
    /// cards one after another, and each needs an id of its own for its `DELETE` to name.
    /// The transfer itself never answers, so a card let go stays in flight.
    std::function<QByteArray(const QByteArray &)> sessions()
    {
        return [this](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import ")) {
                const QByteArray id = "imp_s" + QByteArray::number(++m_opened);
                return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],)"
                                   R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
            }
            return QByteArray();
        };
    }

    /// The sidecar paths of an announcement, sorted: the walk's order is not what is tested.
    static QStringList sidecarsOf(const QJsonObject &body)
    {
        QStringList out;
        for (const QJsonValue &one : body.value(u"sidecars"_s).toArray())
            out << one.toObject().value(u"path"_s).toString();
        out.sort();
        return out;
    }

    static QStringList filesOf(const QJsonObject &body)
    {
        QStringList out;
        for (const QJsonValue &one : body.value(u"files"_s).toArray())
            out << one.toObject().value(u"path"_s).toString();
        out.sort();
        return out;
    }

    /// The announcement whose root is `root`, the last one heard.
    QJsonObject announcementOf(const QString &root) const
    {
        QJsonObject found;
        for (const QJsonObject &body : bodiesOf("POST /import ")) {
            if (body.value(u"root"_s).toString() == root)
                found = body;
        }
        return found;
    }

    int stageOf(int row) const
    {
        return m_imports->data(m_imports->index(row), int(Imports::Role::Stage_)).toInt();
    }

    QString nameOf(int row) const
    {
        return m_imports->data(m_imports->index(row), int(Imports::Role::Name)).toString();
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        QVERIFY(m_folder.isValid());
        m_opened = 0;
        // Emptied, because the sessions an `Imports` keeps live in a file, and the one a
        // previous test left would be dropped by this test's own.
        QSettings(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s, u"preferences"_s)
            .clear();
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(
            QStringLiteral("http://127.0.0.1:%1").arg(m_pretend->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings);
        m_imports = new Imports(m_server);
        m_offers = new ImportOffers(m_imports, m_imports);
        m_queue = new ImportQueue(m_imports, m_imports);
        m_tally = new ImportTally(m_imports, m_imports);
    }

    void cleanup()
    {
        delete m_imports;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    /// A proposal the server is sure of needs no answer: asking anyway would be fifty
    /// clicks for a shelf that was never in doubt.
    void a_file_the_server_is_sure_of_asks_nothing()
    {
        m_pretend->answerFor = [](const QByteArray &) { return aReservation("rcv_1"); };
        m_offers->offer({aFile(u"Tome 1.cbz"_s)});

        QCOMPARE(m_imports->count(), 1);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(m_imports->deciding(), 0);
        QVERIFY2(m_pretend->heard.contains("POST /preflight"), m_pretend->heard.constData());
        // Nothing has been sent: the whole point of proposing first.
        QVERIFY(!m_pretend->heard.contains("PUT /intake"));
    }

    /// One that names nothing stops and waits for a person. `deciding` is what the bar's
    /// button counts while the dialog is shut.
    void a_file_the_server_cannot_place_waits_for_an_answer()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReservation("rcv_2", "UNKNOWN");
        };
        m_offers->offer({aFile(u"scan.cbz"_s)});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));
        QCOMPARE(m_imports->deciding(), 1);

        m_imports->decide(0, u"ed-7"_s);
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(m_imports->deciding(), 0);
    }

    /// The same file twice is one row. Dropping a folder twice is the ordinary accident.
    void the_same_file_offered_twice_is_one_row()
    {
        m_pretend->answerFor = [](const QByteArray &) { return aReservation("rcv_3"); };
        const QString path = aFile(u"Tome 2.cbz"_s);
        m_offers->offer({path, path});
        m_offers->offer({path});

        QCOMPARE(m_imports->count(), 1);
    }

    /// Bytes, then the confirmation, then filed — and the transfer is the one on top.
    void a_file_goes_up_and_is_filed()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_4");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"Tome 3.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-1","path":"x/Tome 3.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"Tome 3.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("PUT /intake/rcv_4/file"),
                 m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("POST /intake/rcv_4/file"),
                 m_pretend->heard.constData());
        QCOMPARE(m_tally->inFlight(), 0);
    }

    /// One at a time. Two on a domestic line steal each other's bandwidth and both finish
    /// later than they would have in series.
    void only_one_goes_up_at_a_time()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_5");
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s), aFile(u"C.cbz"_s)});
        QTRY_COMPARE(stageOf(2), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(stageOf(2), int(Imports::Stage::Ready));
        QVERIFY(m_tally->busy());
    }

    /// The rule the whole queue turns on. Pausing hands the slot to the next in line; it
    /// does not stop anything.
    void pausing_hands_the_slot_to_the_next_in_line()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_6");
            return QByteArray();
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(nameOf(0), u"A.cbz"_s);

        m_queue->pause(0);
        // B took the slot and A stepped behind it — and **neither moved**. The list used to
        // put whoever was sending on top, so a card a reader was looking at slid away
        // because a different one finished. The stage says who is going; the order does not.
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Sending));
        QCOMPARE(nameOf(1), u"B.cbz"_s);
        QCOMPARE(nameOf(0), u"A.cbz"_s);
        QCOMPARE(stageOf(0), int(Imports::Stage::Paused));
    }

    /// And taking it back puts the other one behind — "now it is that one that waits".
    void resuming_takes_the_slot_back_and_the_other_one_waits()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_7");
            return QByteArray();
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_queue->pause(0);
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Sending));

        // A is still at row 0, paused. Resuming it takes the slot back, and again nothing
        // changes place.
        m_queue->resume(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(nameOf(0), u"A.cbz"_s);
        QCOMPARE(nameOf(1), u"B.cbz"_s);
        // Waiting its turn and not paused: nobody stopped it, so nothing is asked of its
        // reader to set it going again once A is done.
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));
        QVERIFY(cardAt(1).road.released);
    }

    /// A pause is a pause. The slot was handed back to a paused file when the one ahead of it
    /// was done — about three seconds later, with nobody having clicked — so a transfer stopped
    /// for the evening was running at midnight. It waits for its reader, however long that is,
    /// and goes when asked.
    void a_paused_file_stays_paused_when_the_one_ahead_finishes()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_8");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"B.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-2","path":"x/B.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_queue->pause(0);
        // B runs to the end, and A is not taken back.
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Filed));
        const int heardThen = m_pretend->heard.count("PUT /intake");
        QTest::qWait(4500);
        QCOMPARE(stageOf(0), int(Imports::Stage::Paused));
        QCOMPARE(m_pretend->heard.count("PUT /intake"), heardThen);
        QCOMPARE(m_imports->data(m_imports->index(0), int(Imports::Role::Fraction)).toDouble()
                     < 1.0,
                 true);

        // Asked for, it goes.
        m_queue->resume(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
    }

    /// The third step lists every card before any moves, and a card nobody pressed is waiting
    /// for its button: the queue used to take any card that was ready the moment the slot was
    /// free, so the line of a series nobody had sent started when the one before it ended.
    void a_line_that_was_not_let_go_is_left_alone_when_the_slot_frees()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_11");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"A.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-3","path":"x/A.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(m_tally->startable(), 2);
        QVERIFY(!m_tally->started());

        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QTest::qWait(400);
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));
        QVERIFY(!cardAt(1).road.released);
        QCOMPARE(m_tally->startable(), 1);
        QVERIFY(m_tally->started());
        QVERIFY(!m_tally->finished());
    }

    /// « Envoi en cours : 21 tomes sur 28 » counted a series nobody had let go: the banner of the
    /// transfers speaks of what was launched, and a line not launched — or put back to zero after
    /// it was — is not part of it.
    void the_sending_counts_only_the_lines_that_were_launched()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_15");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"A.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-7","path":"x/A.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(m_tally->toSendFiles(), 2);
        QCOMPARE(m_tally->sendingFiles(), 0);
        QCOMPARE(m_tally->sendingBytes(), qint64(0));

        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QCOMPARE(m_tally->toSendFiles(), 2);
        QCOMPARE(m_tally->sendingFiles(), 1);
        QCOMPARE(m_tally->sentFiles(), 1);
        QVERIFY(m_tally->sendingBytes() > 0);
        QVERIFY(m_tally->sendingBytes() < m_tally->toSendBytes());
    }

    /// The window ends on « Terminer » only when every line has arrived: a line that failed is
    /// one « Importer tout » can still send again, and a line nobody let go is still to send.
    void a_queue_is_completed_only_when_every_line_has_arrived()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_13");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"A.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-5","path":"x/A.cbz","replacement":false})");
        };
        QVERIFY(!m_tally->completed());
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QVERIFY(!m_tally->completed());

        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(!m_tally->completed(), "the second line was never let go");

        m_queue->start(1);
        QTRY_VERIFY(m_tally->completed());
    }

    /// Closing the window keeps what has not gone and lets go of what has: the lines that
    /// arrived are not the reader's business any more, and a queue that kept them reopened on
    /// the third step, with the button of a send that was over.
    void clearing_the_filed_lines_keeps_the_others()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_14");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"A.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-6","path":"x/A.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));

        const QString kept = cardAt(1).name;
        m_queue->clearFiled();
        QCOMPARE(m_imports->count(), 1);
        QCOMPARE(cardAt(0).name, kept);
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->start(0);
        QTRY_VERIFY(m_tally->completed());
        m_queue->clearFiled();
        QCOMPARE(m_imports->count(), 0);
        QVERIFY(!m_tally->completed());
    }

    /// « Importer tout » lets go every line that is neither moving nor done, in the list's
    /// order and one at a time.
    void importing_everything_lets_every_line_go_in_turn()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_12");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"A.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-4","path":"x/A.cbz","replacement":false})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s), aFile(u"C.cbz"_s)});
        QTRY_COMPARE(stageOf(2), int(Imports::Stage::Ready));

        m_queue->startAll();
        QTRY_VERIFY(m_tally->finished());
        for (int row = 0; row < 3; ++row)
            QCOMPARE(stageOf(row), int(Imports::Stage::Filed));
        QCOMPARE(m_tally->startable(), 0);
        QCOMPARE(m_tally->sentFiles(), 3);
    }

    /// « Importer tout » takes nothing from a line already moving: it was pressed to send what
    /// waits, and a line the reader had started stays where it is.
    void importing_everything_does_not_take_the_slot_from_a_line_already_moving()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_13");
            return QByteArray();
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->start(1);
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Sending));

        m_queue->startAll();
        QCOMPARE(stageOf(1), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(cardAt(0).road.released);
    }

    /// A line sent by its own button goes now, and the one that held the slot waits behind it.
    void sending_a_line_by_its_button_takes_the_slot_and_the_holder_waits()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_14");
            return QByteArray();
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_queue->start(1);
        QCOMPARE(stageOf(1), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
    }

    /// What the third step draws for a line, said by the model: what the card is, how far it
    /// went and whether it is on the list at all.
    void a_line_carries_what_it_says_and_how_far_it_has_gone()
    {
        const QString folder = aFolder();
        // The weight of the two volumes it sends, which are real archives: a figure made up
        // would put the ring past its end.
        const qint64 firstWeight = QFileInfo(folder + u"/Tome 1.cbz"_s).size();
        const qint64 whole = firstWeight + QFileInfo(folder + u"/Tome 2.cbz"_s).size();
        m_pretend->answerFor = [whole](const QByteArray &request) {
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[{"kind":"WORK",)"
                                   R"("name":"Koro Quest","at":""}],"toSend":["Tome 1.cbz",)"
                                   R"("Tome 2.cbz"],"alreadyThere":["Tome 3.cbz"],)"
                                   R"("bytesToSend":)" + QByteArray::number(whole) + "}");
            if (request.startsWith("PUT /import/imp_1/file?path=Tome%201.cbz"))
                return aReply(200, R"({"path":"x","received":9})");
            return QByteArray();  // the second file never lands
        };
        m_offers->offer({folder});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        const QModelIndex first = m_imports->index(0);
        const auto roleOf = [&](Imports::Role role) {
            return m_imports->data(first, int(role));
        };
        QVERIFY(roleOf(Imports::Role::Shown).toBool());
        QVERIFY(!roleOf(Imports::Role::Released).toBool());
        QCOMPARE(roleOf(Imports::Role::Fraction).toDouble(), 0.0);
        const QString about = roleOf(Imports::Role::About).toString();
        QVERIFY2(about.startsWith(u"Série"_s), qPrintable(about));
        QVERIFY2(about.contains(Words::willBeCreated(Manifest::Level::Work)), qPrintable(about));
        QVERIFY2(about.contains(u"JSON Leaf"_s), qPrintable(about));

        m_queue->start(0);
        QTRY_VERIFY(roleOf(Imports::Role::Fraction).toDouble() > 0.4);
        QVERIFY(roleOf(Imports::Role::Fraction).toDouble() < 1.0);
        QVERIFY(roleOf(Imports::Role::Released).toBool());
        QCOMPARE(m_tally->sentFiles(), 1);
        QCOMPARE(m_tally->sentBytes(), firstWeight);
    }

    /// « Annuler » on a line puts it back to zero and keeps it on the list: the server is told
    /// to drop what it received, the folder is announced again, and nothing goes until its
    /// reader sends it — which then goes whole, under the session of the new announcement.
    void cancelling_a_line_puts_it_back_to_zero_and_keeps_it_on_the_list()
    {
        int announcements = 0;
        m_pretend->answerFor = [&announcements](const QByteArray &request) {
            if (request.startsWith("DELETE /import/"))
                return aReply(204, QByteArray());
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":2,"orphans":[],"open":false})");
            if (request.startsWith("POST /import")) {
                const QByteArray id = "imp_c" + QByteArray::number(++announcements);
                return aReply(200, R"({"id":")" + id + R"(","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            }
            if (announcements == 1 && request.startsWith("PUT /import/imp_c1/file?path=Tome%202"))
                return QByteArray();  // the first session stops at its second file
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->start(0);
        QTRY_VERIFY(m_imports->data(m_imports->index(0), int(Imports::Role::Fraction))
                        .toDouble() > 0.4);

        m_queue->cancel(0);
        QCOMPARE(m_imports->count(), 1);
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/imp_c1"),
                     m_pretend->heard.constData());
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(announcements, 2);
        QCOMPARE(cardAt(0).sent, 0);
        QVERIFY(!cardAt(0).road.released);
        QVERIFY(!m_tally->started());
        QCOMPARE(m_imports->data(m_imports->index(0), int(Imports::Role::Fraction)).toDouble(),
                 0.0);

        // Put back to zero is not set going again.
        const int putsThen = m_pretend->heard.count("PUT /import/");
        QTest::qWait(400);
        QCOMPARE(m_pretend->heard.count("PUT /import/"), putsThen);

        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("PUT /import/imp_c2/file?path=Tome%201.cbz"),
                 m_pretend->heard.constData());
    }

    /// Cancelling a card that nobody agreed to yet has nothing to put back: it is still being
    /// checked, and « Retirer » is the way it leaves.
    void cancelling_a_line_that_was_never_let_go_changes_nothing()
    {
        m_pretend->answerFor = [](const QByteArray &) { return aReservation("rcv_15"); };
        m_offers->offer({aFile(u"A.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->cancel(0);
        QTest::qWait(200);
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(!m_pretend->heard.contains("DELETE"));
    }

    /// A line that failed after leaving stays on the list with its reason, counts against the
    /// total as itself, and goes again from its own button — the session is still there, so
    /// what it held is not sent twice.
    void a_line_that_failed_after_leaving_is_sent_again_from_where_it_stopped()
    {
        int commits = 0;
        m_pretend->answerFor = [&commits](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit")) {
                if (commits++ == 0)
                    return aReply(500, R"({"error":"le disque"})");
                return aReply(200, R"({"root":"Koro","installed":2,"orphans":[],"open":false})");
            }
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_f1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));

        const QModelIndex first = m_imports->index(0);
        QVERIFY(m_imports->data(first, int(Imports::Role::Shown)).toBool());
        QVERIFY(m_imports->data(first, int(Imports::Role::Alarm)).toBool());
        QVERIFY(!m_imports->data(first, int(Imports::Role::About)).toString().isEmpty());
        QCOMPARE(m_tally->failedLines(), 1);
        QCOMPARE(m_tally->toSendFiles(), 2);
        QVERIFY(m_tally->finished());
        QCOMPARE(m_tally->startable(), 1);
        QVERIFY(Words::bannerExplains(true, m_tally->finished()).isEmpty());

        const int putsThen = m_pretend->heard.count("PUT /import/");
        m_queue->start(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QCOMPARE(m_pretend->heard.count("PUT /import/"), putsThen);
    }

    /// The banner's sentence on what each line's button does is for a send going on: shown
    /// over a send that was over, it explained buttons nothing could be done with any more.
    void the_banner_drops_its_explanation_once_the_send_is_over()
    {
        QCOMPARE(Words::bannerExplains(false, false), Words::recapExplains());
        QCOMPARE(Words::bannerExplains(true, false), Words::sendingExplains());
        QVERIFY(Words::bannerExplains(true, true).isEmpty());
    }

    /// Out of the queue, and the server told — it keeps what nobody named, so a row that
    /// vanishes from a screen without a word leaves a file on the library's own disk.
    void abandoning_a_row_tells_the_server_to_clean_up()
    {
        m_pretend->answerFor = [](const QByteArray &) { return aReservation("rcv_9"); };
        m_offers->offer({aFile(u"Tome 4.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /intake/rcv_9"),
                     m_pretend->heard.constData());
    }

    /// A local file that went away between being chosen and being wanted is this file's
    /// own failure, and the queue carries on with the next.
    void a_file_that_vanished_fails_alone()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_10");
            return aReply(200, R"({"path":"x","received":9})");
        };
        const QString going = aFile(u"parti.cbz"_s);
        m_offers->offer({going, aFile(u"reste.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QVERIFY(QFile::remove(going));

        m_queue->send();
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Failed)
                    || stageOf(1) == int(Imports::Stage::Failed));
        // The other one was not dragged down with it.
        QTRY_VERIFY(m_tally->inFlight() < 2);
    }

    // ——— Sessions an application quit left on the server ————————————————————————

    /// The next start of the application: the model goes, a new one is built on the same
    /// settings, the way a run that was closed and opened again does.
    void startAgain()
    {
        delete m_imports;
        m_pretend->heard.clear();
        m_imports = new Imports(m_server);
        m_offers = new ImportOffers(m_imports, m_imports);
        m_queue = new ImportQueue(m_imports, m_imports);
        m_tally = new ImportTally(m_imports, m_imports);
    }

    int dropsHeard() const { return int(m_pretend->heard.count("DELETE /import/")); }

    /// A place reserved on the library's disk is kept until somebody says otherwise: three
    /// folders announced and the application closed left three boxes in the inbox for ever.
    void sessions_announced_and_never_sent_are_dropped_on_the_next_start()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s), aFolder(u"Trois"_s)});
        QTRY_COMPARE(m_opened, 3);
        // The server has answered three times; the application has taken the third answer in
        // only when no card is waiting for one. Closing in between left the third id unnoted
        // and dropped two sessions, one run in a few under load.
        QTRY_VERIFY(std::ranges::none_of(m_imports->cards(), [](const Card &one) {
            return one.stage == Card::Stage::Asking;
        }));

        startAgain();

        QTRY_COMPARE(dropsHeard(), 3);
        for (const QByteArray id : {"imp_s1", "imp_s2", "imp_s3"})
            QVERIFY2(m_pretend->heard.contains("DELETE /import/" + id), id.constData());
        // And once is enough: the next start has nothing left to drop.
        startAgain();
        QTest::qWait(300);
        QCOMPARE(dropsHeard(), 0);
    }

    /// What has received bytes is worth what it holds, and the contract lists it for a reason:
    /// dropping it threw away a volume that had taken an hour to send.
    void a_session_that_received_bytes_is_left_alone()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            if (request.startsWith("PUT /import/imp_1/file?path=Tome%201.cbz"))
                return aReply(200, R"({"path":"x","received":9})");
            return QByteArray();  // the second file never lands
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_VERIFY(cardAt(0).sent > 0);

        startAgain();

        QTest::qWait(300);
        QCOMPARE(dropsHeard(), 0);
    }

    /// The first request of a transfer is not yet answered when the application is closed,
    /// and the server has by then kept some of the bytes: dropped at the next start, a session
    /// that was half there went with them.
    void a_session_whose_first_bytes_were_in_flight_is_left_alone()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            return QByteArray();  // the first file never answers
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_VERIFY2(m_pretend->heard.contains("PUT /import/imp_1/file"),
                     m_pretend->heard.constData());

        startAgain();

        QTest::qWait(300);
        QCOMPARE(dropsHeard(), 0);
    }

    /// The ids still kept in the preferences file, as `Imports` wrote them.
    static QStringList keptInTheFile()
    {
        return QSettings(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                         u"preferences"_s)
            .value(u"imports/left"_s)
            .toStringList();
    }

    /// One session left, the next start asks the server to drop it and is answered `status`;
    /// whether it is still kept afterwards. Forgotten whatever the answer, a server that was
    /// off, throttled or choosing a profile at launch left the empty session on its disk for
    /// good.
    bool keptAfterBeingAnswered(int status)
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aFolder()});
        [&] { QTRY_COMPARE(m_opened, 1); }();
        delete m_imports;
        m_pretend->heard.clear();
        if (status == 0) {
            m_pretend->close();
        } else {
            m_pretend->answerFor = [status](const QByteArray &) {
                return aReply(status, R"({"error":"non"})");
            };
        }
        m_imports = new Imports(m_server);
        m_offers = new ImportOffers(m_imports, m_imports);
        m_queue = new ImportQueue(m_imports, m_imports);
        m_tally = new ImportTally(m_imports, m_imports);
        QTest::qWait(500);
        return !keptInTheFile().isEmpty();
    }

    void a_session_is_kept_when_the_server_could_not_be_reached()
    {
        QVERIFY(keptAfterBeingAnswered(0));
    }

    void a_session_is_kept_when_the_server_answers_with_a_failure()
    {
        QVERIFY(keptAfterBeingAnswered(500));
    }

    void a_session_is_kept_when_the_server_asks_for_a_profile_first()
    {
        QVERIFY(keptAfterBeingAnswered(409));
    }

    void a_session_is_kept_when_the_server_asks_to_wait()
    {
        QVERIFY(keptAfterBeingAnswered(429));
    }

    void a_session_the_server_does_not_have_is_forgotten()
    {
        QVERIFY(!keptAfterBeingAnswered(404));
    }

    void a_session_the_server_does_not_let_this_client_drop_is_forgotten()
    {
        QVERIFY(!keptAfterBeingAnswered(403));
    }

    /// The keyring answers after the window is up: an address read at construction is empty,
    /// and a drop sent then went to no server. It waits for the settings to load.
    void sessions_are_dropped_once_the_settings_have_loaded_and_not_before()
    {
        const QString address = m_settings->address();
        QSettings(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s, u"preferences"_s)
            .setValue(u"imports/left"_s, QStringList{Server::tidy(address) + u" imp_late"_s});

        Settings later;
        Server server(&later);
        later.setAddress(address);
        later.setKey(u"8f3a92c1d4e5b6a7"_s);
        QVERIFY(!later.loaded());
        m_pretend->answerFor = [](const QByteArray &) { return aReply(204, {}); };
        m_pretend->heard.clear();
        Imports imports(&server);
        QTest::qWait(50);
        if (!later.loaded())
            QVERIFY(!m_pretend->heard.contains("DELETE"));
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/imp_late"),
                     m_pretend->heard.constData());
    }

    /// An installed folder has no session to drop: it was asked for a second time and the
    /// answer was a 404 nobody read.
    void a_committed_session_is_not_remembered()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":2,"orphans":[],"open":false})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));

        startAgain();

        QTest::qWait(300);
        QCOMPARE(dropsHeard(), 0);
    }

    /// A folder given up on is gone from the server: its id is not asked for again at the
    /// next start.
    void a_session_given_up_is_not_dropped_a_second_time()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(m_opened, 2);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_imports->abandon(0);
        QTRY_VERIFY(m_pretend->heard.contains("DELETE /import/imp_s1"));

        startAgain();

        QTRY_COMPARE(dropsHeard(), 1);
        QVERIFY2(m_pretend->heard.contains("DELETE /import/imp_s2"),
                 m_pretend->heard.constData());
    }

    /// The same file serves every library this client has been linked to, and a session
    /// belongs to the server that opened it: dropped against another, it would delete a
    /// stranger's id that happened to be the same.
    void a_session_of_another_server_is_kept_for_it()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aFolder()});
        QTRY_COMPARE(m_opened, 1);
        const QString here = m_settings->address();

        m_settings->setAddress(u"http://127.0.0.1:1"_s);
        startAgain();
        QTest::qWait(300);
        QCOMPARE(dropsHeard(), 0);

        m_settings->setAddress(here);
        startAgain();
        QTRY_COMPARE(dropsHeard(), 1);
    }

    // ——— The folder road ——————————————————————————————————————————————————————

    /// A folder is announced whole and sent file by file. What the server already holds is
    /// not sent again, which is the whole reason for announcing first.
    void a_folder_is_announced_then_sent_file_by_file()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":2,"orphans":[],"open":false})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("PUT /import/imp_1/file?path=Tome%201.cbz"),
                 m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("PUT /import/imp_1/file?path=Tome%202.cbz"),
                 m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("POST /import/imp_1/commit"),
                 m_pretend->heard.constData());
        // The one the server already had never left this machine.
        QVERIFY(!m_pretend->heard.contains("Tome%203.cbz"));
    }

    /// A folder's own session lives under `/import/{id}`, never `/intake/{id}` — that route
    /// is for a single reserved file, and `received_folder` on the server refuses an
    /// `imp_…` id there through `Origin::of`. Sending every abandon down `/intake/`
    /// regardless of `Row::folder` meant a folder's cleanup always failed, silently — the
    /// bytes it had already received stayed in the inbox forever, one box per abandoned
    /// folder.
    void abandoning_a_folder_cleans_up_through_the_folder_route()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            return aReply(200, R"({"id":"imp_70","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/imp_70"),
                     m_pretend->heard.constData());
        QVERIFY2(!m_pretend->heard.contains("DELETE /intake/imp_70"),
                 m_pretend->heard.constData());
    }

    /// The row is gone from the screen the instant `abandon()` is called either way, but a
    /// cleanup the server refused is not the same fact as one that happened — before this
    /// existed the callback threw the answer away regardless, so both looked identical,
    /// which is exactly the box of bytes nothing afterwards would ever sweep up. It comes
    /// back as itself, failed, rather than as a queue-wide `trouble` — see the next test
    /// for why that distinction is the one that matters.
    void a_cleanup_the_server_refused_comes_back_as_a_failed_card()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            // 500, not 403: a 403 is `Server`'s own signal that the key itself lost its
            // right to import — `Server::send`'s `m_stopped` — which is a fact about every
            // future request, not about this one session. This test is about a refusal
            // that is this card's own, so it needs a status that carries no such meaning.
            if (request.startsWith("DELETE"))
                return aReply(500, R"({"error":"internal error"})");
            return aReply(200, R"({"id":"imp_71","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(m_imports->trouble().isEmpty());

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);

        QTRY_COMPARE(m_imports->count(), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Failed));
        const QString trouble =
            m_imports->data(m_imports->index(0), int(Imports::Role::Trouble)).toString();
        QVERIFY2(!trouble.isEmpty(), "a refused cleanup came back saying nothing");
        // The queue-wide property stays clear: this is one card's own fact, not a reason
        // for `pump()` to stop looking at the rest of them.
        QVERIFY(m_imports->trouble().isEmpty());
    }

    /// The measured decision this round corrects: a refused cleanup used to set the
    /// queue-wide `trouble`, which `pump()` reads before starting anything — so abandoning
    /// one card whose session the server would not drop silently stopped every other row
    /// from ever sending, for the rest of the session, with nothing to re-arm it. Abandoning
    /// a card says nothing about whether the rest of the queue can still send.
    void abandoning_one_card_does_not_stop_the_rest_of_the_queue_from_sending()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight")) {
                return aReservation(request.contains("\"name\":\"A.cbz\"") ? "rcv_20"
                                                                            : "rcv_21");
            }
            // 500, not 403: see the previous test for why a 403 would not isolate this.
            if (request.startsWith("DELETE /intake/rcv_20"))
                return aReply(500, R"({"error":"internal error"})");
            if (request.startsWith("PUT /intake/rcv_21"))
                return aReply(200, R"({"path":"B.cbz","received":9})");
            if (request.startsWith("POST /intake/rcv_21"))
                return aReply(200, R"({"entryId":"en-9","path":"x/B.cbz","replacement":false})");
            return aReply(200, R"({})");
        };
        m_offers->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));

        // A's cleanup is refused in the background — B does not wait on it.
        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 1);
        QCOMPARE(nameOf(0), u"B.cbz"_s);

        m_queue->send();
        QTRY_VERIFY2(stageOf(0) == int(Imports::Stage::Filed), m_pretend->heard.constData());
        QVERIFY(m_imports->trouble().isEmpty());

        // And A comes back, on its own, once its own refusal has landed — proof the
        // send above ran without ever being blocked by it, not proof it never happened.
        QTRY_COMPARE(m_imports->count(), 2);
        QCOMPARE(nameOf(1), u"A.cbz"_s);
        QCOMPARE(stageOf(1), int(Imports::Stage::Failed));
    }

    /// The server builds `creates` from sidecars alone (`bulk_import.rs::would_create`), so
    /// a folder of archives with no declaration at all — the spec's own "implicit edition"
    /// — never appears in it. Before this fix, a container absent from both `creates` and
    /// `moves` fell straight to "already in the library" the moment the server had
    /// answered at all — true for a series it already knew, and false here: this card's own
    /// volumes are all in `toSend`, and nothing about it is in the library yet.
    void an_undeclared_folder_does_not_claim_to_be_already_there()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_80","root":"Bleach","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":27})");
        };
        const QString at = m_folder.filePath(u"Bleach"_s);
        QVERIFY(QDir().mkpath(at));
        for (const QString &volume : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s}) {
            QFile file(at + u'/' + volume);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(9, 'x'));
        }

        m_offers->offer({at});
        // Undeclared, so its series' name is guessed from the folder and the card stops to
        // be checked — after the answer, which is what this test reads.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QCOMPARE(nodes.size(), 1);
        QVERIFY2(nodes.constFirst().toMap().value(u"state"_s).toString().isEmpty(),
                 "an undeclared, brand new folder claimed to be already in the library");
    }

    /// The measured defect: each 4 MiB chunk of a folder's send invalidated every role a
    /// card reads — `Role::Nodes` included — so `flatten` rebuilt the whole unfolded tree
    /// from scratch, `node.volumes()` walked afresh at every node, and the `Repeater`
    /// behind it destroyed and recreated every `ImportNode` once per chunk rather than once
    /// per volume. On a real 7.9 GiB folder that is about two thousand rebuilds — the same
    /// defect `2614544` fixed for the hash tick, left on the send path.
    void a_send_chunk_only_announces_the_roles_it_changed()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit"))
                return aReply(200,
                              R"({"root":"Grand","installed":2,"orphans":[],"open":false})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_90","root":"Grand","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":5242889})");
            return aReply(200, R"({"path":"x","received":0})");
        };
        const QString at = m_folder.filePath(u"Grand"_s);
        QVERIFY(QDir().mkpath(at));
        // Larger than the 4 MiB chunk size, so one file crosses two PUTs: the first stays
        // inside it, the second finishes it.
        aDeclaration(at, "Grand");
        aVolume(at + u"/Tome 1.cbz"_s, 1, 5 * 1024 * 1024);
        aVolume(at + u"/Tome 2.cbz"_s, 2);

        QList<QList<int>> sentRoles;
        const auto connection = connect(
            m_imports, &QAbstractItemModel::dataChanged, m_imports,
            [&sentRoles](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.contains(int(Imports::Role::Sent)))
                    sentRoles << roles;
            });

        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QObject::disconnect(connection);

        QVERIFY2(!sentRoles.isEmpty(), "no scoped Sent update seen at all");
        bool sawSentAlone = false;
        bool sawSentWithNodes = false;
        for (const QList<int> &roles : sentRoles) {
            if (roles.size() == 1 && roles.constFirst() == int(Imports::Role::Sent))
                sawSentAlone = true;
            if (roles.contains(int(Imports::Role::Nodes)))
                sawSentWithNodes = true;
        }
        QVERIFY2(sawSentAlone,
                 "a chunk that stayed inside one file still touched Role::Nodes");
        QVERIFY2(sawSentWithNodes,
                 "the chunk that finished a file never touched Role::Nodes");
    }

    /// The declarations travel in the announcement, not among the files. A folder *is* its
    /// declaration, and the server has to read it before it can say what it would create.
    void the_declaration_travels_in_the_announcement()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_2","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY2(m_pretend->heard.contains("\"sidecars\""), m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("work.json"), m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("Koro Quest"), m_pretend->heard.constData());
    }

    /// A folder that would create its series is named for it and does not stop.
    ///
    /// It used to wait in `Deciding` for somebody to say yes, which meant a folder whose
    /// every value was said still stopped the moment its series was new — and every series
    /// is new once. What it will create is still on the card, read before anybody presses
    /// « Importer », which is the yes.
    void a_folder_that_would_create_something_is_named_and_does_not_stop()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200,
                          R"({"id":"imp_3","root":"Koro","creates":)"
                          R"([{"kind":"WORK","name":"Koro Quest","at":""}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        const QVariantList made = m_imports->data(m_imports->index(0, 0),
                                                  int(Imports::Role::Creates)).toList();
        QCOMPARE(made.size(), 1);
        QCOMPARE(made.constFirst().toMap().value(u"name"_s).toString(), u"Koro Quest"_s);
    }

    /// A folder read from a Komga library is announced, then stops to be checked.
    ///
    /// Announced first because what is identical to the library comes back in the answer and
    /// nowhere else; stopped after, because a value nobody said — here the reading direction
    /// — is a value somebody has to look at before it is written into the library for good.
    void a_komga_folder_stops_to_be_reviewed_after_being_announced()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_k","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],"alreadyThere":[],)"
                               R"("bytesToSend":18})");
        };
        m_offers->offer({aKomgaFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY2(m_pretend->heard.contains("POST /import "), m_pretend->heard.constData());
        QCOMPARE(m_imports->reviewing(), 1);
        // Stopped is not still being looked at: the start button waits on `reading`, and a
        // card waiting for a person must not hold it as if the disk were still being read.
        QCOMPARE(m_tally->reading(), 0);
        QCOMPARE(m_imports->deciding(), 0);
        QCOMPARE(Words::importStage(Words::Importing::Reviewing), u"À vérifier"_s);
    }

    /// What was read travels in the announcement: the `work.json` composed from the
    /// ComicInfo, and on each archive the rewrite that puts an `entry.json` in it and takes
    /// the ComicInfo out. Without it the server would file a Komga folder with the folder's
    /// name for a title and no numbers at all.
    void the_announcement_carries_the_composed_work_json_and_each_rewrite()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_r","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],"alreadyThere":[],)"
                               R"("bytesToSend":18})");
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QList<QJsonObject> bodies = bodiesOf("POST /import ");
        QCOMPARE(bodies.size(), 1);
        const QJsonArray sidecars = bodies.constFirst().value(u"sidecars"_s).toArray();
        QVERIFY2(!sidecars.isEmpty(), "no work.json composed from what the ComicInfo said");
        QCOMPARE(sidecars.at(0).toObject().value(u"path"_s).toString(), u"work.json"_s);
        const QJsonObject work = QJsonDocument::fromJson(
            sidecars.at(0).toObject().value(u"json"_s).toString().toUtf8()).object();
        QCOMPARE(work.value(u"title"_s).toString(), u"Ao Haru Ride"_s);

        const QJsonArray files = bodies.constFirst().value(u"files"_s).toArray();
        QCOMPARE(files.size(), 2);
        for (const QJsonValue &one : files) {
            const QJsonObject rewrite = one.toObject().value(u"rewrite"_s).toObject();
            QCOMPARE(rewrite.value(u"remove"_s).toArray(), QJsonArray{u"ComicInfo.xml"_s});
            const QJsonObject entry =
                QJsonDocument::fromJson(rewrite.value(u"entryJson"_s).toString().toUtf8())
                    .object();
            QVERIFY2(entry.value(u"number"_s).isDouble(),
                     qPrintable(rewrite.value(u"entryJson"_s).toString()));
        }
    }

    /// A series the library already holds whole is not stopped by what was guessed about
    /// it: nothing of it will be sent, so nothing about it will be written, and asking a
    /// reader to check values that go nowhere is a question with no consequence.
    void a_series_the_library_holds_whole_is_not_stopped_by_its_guesses()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_w","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":[],"alreadyThere":["Tome 1.cbz","Tome 2.cbz"],)"
                               R"("bytesToSend":0})");
        };
        m_offers->offer({aKomgaFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(m_imports->reviewing(), 0);
    }

    /// A correction stays on this side until « Importer »: then the first session is
    /// dropped and the folder announced again, carrying it. Sent as it is typed, every key
    /// pressed would be one more session for the server to open and forget.
    void an_edit_reaches_the_announcement_only_when_confirmed()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_e","root":"Ao Haru Ride","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        m_pretend->heard.clear();
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        sayWhatIsLacking(0);
        QTest::qWait(100);
        QVERIFY2(m_pretend->heard.isEmpty(), m_pretend->heard.constData());
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        const qsizetype dropped = m_pretend->heard.indexOf("DELETE /import/imp_e");
        QVERIFY2(dropped >= 0, m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.indexOf("POST /import ") > dropped,
                 m_pretend->heard.constData());
        const QList<QJsonObject> bodies = bodiesOf("POST /import ");
        QCOMPARE(bodies.size(), 1);
        const QJsonObject work = QJsonDocument::fromJson(bodies.constFirst()
                                                             .value(u"sidecars"_s)
                                                             .toArray()
                                                             .at(0)
                                                             .toObject()
                                                             .value(u"json"_s)
                                                             .toString()
                                                             .toUtf8())
                                     .object();
        QCOMPARE(work.value(u"title"_s).toString(), u"Ao"_s);
    }

    /// « Suivant » over the checking step makes a stopped card ready and sends nothing: the
    /// card is still corrected from the step after it, still unconfirmed, and nothing has
    /// been let go until « Importer ».
    void preparing_makes_a_stopped_card_ready_and_sends_nothing()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_p","root":"Ao Haru Ride","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            return QByteArray();
        };
        m_offers->offer({aGuessedFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        m_pretend->heard.clear();

        m_queue->prepare();

        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(!cardAt(0).road.confirmed);
        QCOMPARE(m_tally->launched(), 0);
        QCOMPARE(m_imports->reviewing(), 0);
        QTest::qWait(100);
        QVERIFY2(m_pretend->heard.isEmpty(), m_pretend->heard.constData());
        // Still corrected from there: the card goes back to being checked.
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// A card corrected is left as it is by « Suivant »: its announcement is the one thing
    /// that starts by itself, and it waits for « Importer » like the rest. It is not
    /// `blocked`, so the window goes on to the third step with it.
    void preparing_leaves_a_corrected_card_for_importer()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_c","root":"Ao Haru Ride","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            return QByteArray();
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        sayWhatIsLacking(0);
        m_pretend->heard.clear();

        m_queue->prepare();

        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(m_tally->blocked(), 0);
        QTest::qWait(100);
        QVERIFY2(m_pretend->heard.isEmpty(), m_pretend->heard.constData());
        QCOMPARE(m_tally->launched(), 0);
        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QVERIFY(m_tally->launched() > 0);
    }

    /// A re-announcement clears what was ticked, like every announcement: the session the
    /// tick was given against is gone, and a tick carried over would write over a
    /// declaration for a reason the new answer may no longer give.
    void a_reannouncement_clears_what_was_ticked()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_t","root":"Koro","creates":[],)"
                                   R"("declarations":[{"path":"work.json","present":{},)"
                                   R"("presentName":"Koro Quest","differs":["title"]}],)"
                                   R"("toSend":["Tome 1.cbz"],)"
                                   R"("alreadyThere":["Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("bytesToSend":9})");
            return QByteArray();
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->setDeclaring(0, u"work.json"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Declaring, u"work.json"_s));

        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Koro"_s));
        m_queue->confirm();
        QTRY_COMPARE(bodiesOf("POST /import ").size(), 2);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Declaring, u"work.json"_s));
    }

    /// A card corrected, abandoned and dropped again is read afresh: the new card is a new
    /// reading of the disk, and a correction surviving on it would be a value nobody can
    /// see being typed.
    void an_abandoned_card_redropped_is_read_afresh()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            return aReply(200, R"({"id":"imp_a","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],"alreadyThere":[],)"
                               R"("bytesToSend":18})");
        };
        const QString at = aKomgaFolder();
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        QVERIFY(cardAt(0).road.edited);

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(!cardAt(0).road.edited);
        const Facts::Field title = fieldOf(0, QString(), u"title"_s);
        QCOMPARE(title.value.toString(), u"Ao Haru Ride"_s);
        QVERIFY(!title.edited);
    }

    /// « Importer » lets go of every card it can and holds back the one it cannot: two
    /// volumes claiming one number would leave the server to keep one of them, and nobody
    /// would know which. The other series has no reason to wait for that one.
    void a_blocked_series_stays_to_be_reviewed_when_the_rest_goes()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import ") && request.contains("Doublon"))
                return aReply(200, R"({"id":"imp_d1","root":"Doublon","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 1 bis.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_d2","root":"Ao Haru Ride","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            return QByteArray();
        };
        const QString twice = m_folder.filePath(u"Doublon"_s);
        QVERIFY(QDir().mkpath(twice));
        aDeclaration(twice, "Doublon");
        aVolume(twice + u"/Tome 1.cbz"_s, 1);
        aVolume(twice + u"/Tome 1 bis.cbz"_s, 1);
        m_offers->offer({twice, aGuessedFolder()});
        QTRY_COMPARE(m_imports->reviewing(), 2);

        m_queue->confirm();
        const int held = rowNamed(u"Doublon"_s);
        const int gone = rowNamed(u"Ao Haru Ride"_s);
        QVERIFY(held >= 0 && gone >= 0);
        QTRY_COMPARE(stageOf(gone), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(held), int(Imports::Stage::Reviewing));
        QCOMPARE(m_imports->reviewing(), 1);
    }

    /// Once the bytes are on their way, a correction could only reach the library by a
    /// second upload nobody asked for: it is refused rather than kept somewhere it does
    /// nothing.
    void an_edit_after_sending_started_changes_nothing()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_s","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],)"
                                   R"("alreadyThere":["Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("bytesToSend":9})");
            return QByteArray();
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        QVERIFY(!m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Autre"_s));
        QCOMPARE(stageOf(0), int(Imports::Stage::Sending));
        QVERIFY(!cardAt(0).road.edited);
        QCOMPARE(fieldOf(0, QString(), u"title"_s).value.toString(), u"Koro Quest"_s);
    }

    /// A card whose transfer failed half-way and came back `Ready` has bytes on the server
    /// already: the stage alone said it could still be corrected, and a correction then
    /// would have dropped a session holding a volume to send it all again.
    void a_card_ready_again_after_failing_half_way_refuses_an_edit()
    {
        bool refused = false;
        m_pretend->answerFor = [&refused](const QByteArray &request) {
            if (request.startsWith("POST /import ") && request.contains("\"root\":\"Un\""))
                return aReply(200, R"({"id":"imp_h1","root":"Un","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 3.cbz"],"bytesToSend":18})");
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_h2","root":"Deux","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],)"
                                   R"("alreadyThere":["Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("bytesToSend":9})");
            if (request.startsWith("PUT /import/imp_h1/file?path=Tome%202.cbz") && !refused) {
                refused = true;
                return aReply(500, R"({"error":"le disque"})");
            }
            if (request.startsWith("PUT /import/imp_h1"))
                return aReply(200, R"({"path":"x","received":9})");
            return QByteArray();  // the second card holds the slot and never finishes
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Paused));
        QVERIFY(!m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Autre"_s));
        // The countdown ends while the other card holds the slot: `Ready`, and not sending.
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Sending));
        QTRY_COMPARE_WITH_TIMEOUT(stageOf(0), int(Imports::Stage::Ready), 10000);

        QVERIFY(!m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Autre"_s));
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(!cardAt(0).road.edited);
    }

    /// A re-announcement whose `DELETE` the server refuses fails the card and says why, in
    /// the words `abandon` uses: the session it meant to drop is still there, and announcing
    /// over it would leave two.
    void a_refused_drop_during_a_reannouncement_fails_the_card_and_says_so()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(500, R"({"error":"internal error"})");
            return aReply(200, R"({"id":"imp_x","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],"alreadyThere":[],)"
                               R"("bytesToSend":18})");
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        sayWhatIsLacking(0);

        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QCOMPARE(m_imports->data(m_imports->index(0), int(Imports::Role::Trouble)).toString(),
                 Words::couldNotCleanUp(nameOf(0)));
        QCOMPARE(bodiesOf("POST /import ").size(), 1);
        // And it still names its session, so abandoning it asks the server again.
        QCOMPARE(cardAt(0).id, u"imp_x"_s);
        QVERIFY(m_imports->trouble().isEmpty());
    }

    /// Abandoned while its session was being dropped for a re-announcement, and the drop
    /// refused: `abandon` asked nothing, the id having already been let go, so the refusal
    /// comes back as the failed card — said to nobody, it was a session left on the server.
    void a_card_abandoned_during_a_refused_drop_comes_back_failed()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(500, R"({"error":"internal error"})");
            return aReply(200, R"({"id":"imp_y","root":"Ao Haru Ride","creates":[],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],"alreadyThere":[],)"
                               R"("bytesToSend":18})");
        };
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("DELETE");
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QString name = nameOf(0);
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        sayWhatIsLacking(0);
        m_queue->confirm();
        QTRY_VERIFY(m_pretend->heard.contains("DELETE /import/imp_y"));

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);
        m_pretend->release();

        QTRY_COMPARE(m_imports->count(), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Failed));
        QCOMPARE(m_imports->data(m_imports->index(0), int(Imports::Role::Trouble)).toString(),
                 Words::couldNotCleanUp(name));
        QCOMPARE(m_pretend->heard.count("DELETE /import/imp_y"), 1);
        QCOMPARE(bodiesOf("POST /import ").size(), 1);
    }

    /// Two files at one number, both already in the library, do not hold a card back: they
    /// are not sent, so nothing about them is written. Weighed apart from what stopped the
    /// card, they kept it in `Reviewing` with nothing a reader could correct.
    void a_duplicate_among_files_already_there_does_not_hold_a_card()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_dd","root":"Doublon","creates":[],)"
                                   R"("toSend":["Tome 2.cbz"],)"
                                   R"("alreadyThere":["Tome 1.cbz","Tome 1 bis.cbz"],)"
                                   R"("bytesToSend":9})");
            return QByteArray();
        };
        const QString at = m_folder.filePath(u"Doublon"_s);
        QVERIFY(QDir().mkpath(at));
        aDeclaration(at, "Doublon");
        aVolume(at + u"/Tome 1.cbz"_s, 1);
        aVolume(at + u"/Tome 1 bis.cbz"_s, 1);
        // An `entry.json` that is no object: what stops the card, on a file that travels.
        QFile guessed(at + u"/Tome 2.cbz"_s);
        QVERIFY(guessed.open(QIODevice::WriteOnly));
        guessed.write(Zip::volume("[]"));
        guessed.close();
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
    }

    /// « Importer » over the checking step is not the tick a series that would move asks
    /// for: confirmed, re-announced or not, such a card stops in `Deciding` and waits for it.
    void a_confirmed_card_that_would_move_a_series_still_waits_for_its_tick()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            const QByteArray root = request.contains("\"root\":\"Blue\"") ? "Blue" : "Ao Haru Ride";
            return aReply(200, R"({"id":"imp_m)" + root.left(2) + R"(","root":")" + root
                                   + R"(","creates":[],"moves":[{"workId":"w-1","matched":"IDENTITY","name":")"
                                   + root + R"(","from":"Mangas/x","at":""}],)"
                                     R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                     R"("bytesToSend":9})");
        };
        m_offers->offer({aGuessedFolder(), aGuessedFolder(u"Blue"_s, 1)});
        QTRY_COMPARE(m_imports->reviewing(), 2);
        const int asIs = rowNamed(u"Ao Haru Ride"_s);
        const int corrected = rowNamed(u"Blue"_s);
        QVERIFY(asIs >= 0 && corrected >= 0);
        QVERIFY(m_imports->edit(cardAt(corrected).token, QString(), u"title"_s, u"Blue!"_s));

        m_queue->confirm();
        QCOMPARE(stageOf(asIs), int(Imports::Stage::Deciding));
        QTRY_COMPARE(bodiesOf("POST /import ").size(), 3);
        QTRY_COMPARE(stageOf(corrected), int(Imports::Stage::Deciding));
        QCOMPARE(m_imports->deciding(), 2);
        QVERIFY2(!m_pretend->heard.contains("PUT /import"), m_pretend->heard.constData());
    }

    /// Between dropping its session and the new answer, a re-announced card is being looked
    /// at again: the start button waits on `reading`, and a card counted nowhere in that
    /// window would let the dialog close on a transfer that had not begun.
    void a_reannouncement_counts_as_being_read()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_q","root":"Ao Haru Ride","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            return QByteArray();
        };
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("DELETE");
        };
        m_offers->offer({aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Ao"_s));
        sayWhatIsLacking(0);

        m_queue->confirm();
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));
        QCOMPARE(m_tally->reading(), 1);
        QCOMPARE(m_imports->reviewing(), 0);
        QTRY_VERIFY(m_pretend->heard.contains("DELETE /import/imp_q"));
        QCOMPARE(m_tally->reading(), 1);

        m_pretend->release();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(m_tally->reading(), 0);
    }

    // ——— What goes while the rest is read ————————————————————————————————————————

    /// One session per announcement, three volumes to send, each taken and each folder
    /// filed: a card can reach the library while another is still read.
    std::function<QByteArray(const QByteArray &)> filingEach()
    {
        return [this](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"x","installed":3,"orphans":[],"open":false})");
            if (request.startsWith("POST /import ")) {
                const QByteArray id = "imp_f" + QByteArray::number(++m_opened);
                return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":27})");
            }
            if (request.startsWith("PUT "))
                return aReply(200, R"({"path":"x","received":9})");
            return QByteArray();
        };
    }

    /// Holds back the announcement of the folder `root`: walked, it waits for the server's
    /// answer for as long as a test needs it to.
    static std::function<bool(const QByteArray &)> announcing(const QByteArray &root)
    {
        return [root](const QByteArray &request) {
            return request.startsWith("POST /import ")
                   && request.contains("\"root\":\"" + root + '"');
        };
    }

    /// The row of the folder dropped as `name`: the card's own name is its series' title.
    int rowOfFolder(const QString &name) const
    {
        for (int row = 0; row < m_imports->count(); ++row) {
            if (QFileInfo(cardAt(row).path).fileName() == name)
                return row;
        }
        return -1;
    }

    /// At fifty series the walk outlasts the first « Importer »: a complete series read
    /// after the press waited « Prête » for a second press nobody knew to make, once the
    /// ones sent before it were filed and nothing was left to start it. And the first card
    /// went the whole way while the second was still read.
    void a_complete_series_read_after_the_press_goes_in_its_turn()
    {
        m_pretend->answerFor = filingEach();
        m_pretend->holdBack = announcing("Deux");
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_VERIFY(cardAt(1).road.described);

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QCOMPARE(stageOf(1), int(Imports::Stage::Asking));

        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Filed));
    }

    /// The press agreed to what had been seen: a series read after it that asks to be
    /// checked stops there, as it would have without the press, and sends nothing.
    void a_series_to_review_read_after_the_press_stops()
    {
        m_pretend->answerFor = filingEach();
        m_pretend->holdBack = announcing("Ao Haru Ride");
        m_offers->offer({aFolder(u"Un"_s), aKomgaFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_VERIFY(cardAt(1).road.described);

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Reviewing));
        QTest::qWait(200);
        QCOMPARE(stageOf(1), int(Imports::Stage::Reviewing));
        QVERIFY(!cardAt(1).road.confirmed);
        QVERIFY2(!m_pretend->heard.contains("PUT /import/imp_f2"), m_pretend->heard.constData());
    }

    /// Read after the press and holding nothing the library lacks, a series is let go as
    /// the press lets go of those read before it, and its session is dropped: left « Prête »
    /// over nothing, it was one more box in the server's inbox.
    void an_identical_series_read_after_the_press_is_let_go_and_its_session_dropped()
    {
        const auto filing = filingEach();
        m_pretend->answerFor = [filing](const QByteArray &request) {
            if (request.startsWith("POST /import ") && request.contains("\"root\":\"Deux\""))
                return aReply(200, R"({"id":"imp_held","root":"Deux","creates":[],)"
                                   R"("toSend":[],"alreadyThere":)"
                                   R"(["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("bytesToSend":0})");
            return filing(request);
        };
        m_pretend->holdBack = announcing("Deux");
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_VERIFY(cardAt(1).road.described);

        m_queue->send();
        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/imp_held"),
                     m_pretend->heard.constData());
        QTRY_COMPARE(m_imports->count(), 1);
        QCOMPARE(rowOfFolder(u"Deux"_s), -1);
        QTRY_COMPARE(stageOf(rowOfFolder(u"Un"_s)), int(Imports::Stage::Filed));
    }

    /// One card on its way, and the announcement of another refused meanwhile: the refused
    /// one fails and leaves its drop, and the one on its way is not stopped for it.
    void a_card_on_its_way_carries_on_when_another_is_refused_meanwhile()
    {
        const auto filing = filingEach();
        m_pretend->answerFor = [filing](const QByteArray &request) {
            if (request.startsWith("POST /import ") && request.contains("\"root\":\"Deux\""))
                return aReply(400, R"({"error":"refused"})");
            return filing(request);
        };
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("PUT ") || announcing("Deux")(request);
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_VERIFY(cardAt(1).road.described);
        m_queue->send();
        QTRY_VERIFY2(m_pretend->heard.contains("PUT /import/imp_f1"), m_pretend->heard.constData());
        QCOMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Failed));
        QCOMPARE(cardAt(1).road.drop, quint64(0));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY(m_imports->trouble().isEmpty());
    }

    /// Abandoned while its files are on their way, a card asks the server to drop its
    /// session, and the one queued behind it takes the free place.
    void abandoning_a_card_on_its_way_drops_its_session_and_lets_the_next_go()
    {
        m_pretend->answerFor = filingEach();
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("PUT /import/imp_f1");
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_VERIFY2(m_pretend->heard.contains("PUT /import/imp_f1"), m_pretend->heard.constData());
        QCOMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_imports->abandon(0);
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/imp_f1"),
                     m_pretend->heard.constData());
        QCOMPARE(rowOfFolder(u"Deux"_s), 0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTest::qWait(100);
        QCOMPARE(m_imports->count(), 1);
        QVERIFY(m_imports->trouble().isEmpty());
    }

    /// The application quit while one card's first bytes were on their way and another
    /// waited its turn: the next start drops the session that received nothing, and never
    /// the one the server may already hold bytes of.
    void quitting_mid_transfer_drops_only_the_session_that_received_nothing()
    {
        const auto filing = filingEach();
        m_pretend->answerFor = [filing](const QByteArray &request) {
            // The transfer never answers: its bytes may or may not have landed.
            if (request.startsWith("PUT "))
                return QByteArray();
            return filing(request);
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_VERIFY2(m_pretend->heard.contains("PUT /import/imp_f1"), m_pretend->heard.constData());
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));

        startAgain();

        QTRY_COMPARE(dropsHeard(), 1);
        QVERIFY2(m_pretend->heard.contains("DELETE /import/imp_f2"),
                 m_pretend->heard.constData());
        QTest::qWait(200);
        QCOMPARE(dropsHeard(), 1);
    }

    /// A card that was ready and is corrected goes back to be checked: the announcement it
    /// was ready with no longer says what will be written, and only « Importer » sends the
    /// one that does.
    void editing_a_ready_card_sends_it_back_to_review()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_b","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Autre"_s));
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(m_imports->reviewing(), 1);
        QVERIFY(cardAt(0).road.edited);
    }

    /// A folder is read off this thread, and `offer` returns before a byte of any volume
    /// has been checksummed.
    ///
    /// Measured, on a real series dropped onto the real window: the checksummed walk ran
    /// on the thread drawing it, and the application froze until the desktop offered to
    /// kill it — before anything had been sent. Checksums make it minutes rather than
    /// seconds, and the walk alone is enough on a folder of any size.
    ///
    /// Asserted on the state rather than on a stopwatch: when `offer` comes back the row is
    /// there, already sized — `Manifest::found` costs nothing but a `stat` of each file,
    /// so the tree knows its weight before a single checksum — and it still says it is
    /// being looked at. The answer arrives on the event loop, which is not running inside
    /// `offer`, so this cannot race.
    void a_folder_is_read_off_the_thread_that_draws_the_window()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_12","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->setVerifying(true);

        const QString koro = aFolder();
        m_offers->offer({koro});
        QCOMPARE(m_imports->rowCount({}), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));
        // Three volumes, their weight known from finding alone — no checksum computed yet.
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Size)).toLongLong(),
                 weightOf(koro));
        // And the queue knows it is not ready to start. Pressing « Démarrer » here would
        // find nothing ready, start nothing, and close the dialog on a transfer that never
        // began.
        QCOMPARE(m_tally->reading(), 1);

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(m_tally->reading(), 0);
    }

    /// The defect measured: dropping the folder that holds every series rendered one card
    /// named after it. It renders one per declared thing instead, and the shelf itself is
    /// nowhere in the list.
    ///
    /// Each series declares itself: two folders of archives that say nothing, under a folder
    /// whose name is no shelf's, are what the shape reads as a universe to confirm — the
    /// reading `dropping_a_shelf_of_unsaid_folders_makes_one_card_per_reading` is about.
    void a_shelf_gives_one_card_per_declared_thing()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_20","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const QString shelf = m_folder.filePath(u"Dépôt"_s);
        QVERIFY(QDir().mkpath(shelf));
        for (const QString &series : {u"Death Note"_s, u"Bleach"_s}) {
            const QString at = shelf + u'/' + series;
            QVERIFY(QDir().mkpath(at));
            aDeclaration(at, series.toUtf8());
            QFile volume(at + u"/Tome 1.cbz"_s);
            QVERIFY(volume.open(QIODevice::WriteOnly));
            volume.write(QByteArray(9, 'x'));
        }

        m_offers->offer({shelf});

        QCOMPARE(m_imports->rowCount({}), 2);
        QCOMPARE(nameOf(0), u"Bleach"_s);
        QCOMPARE(nameOf(1), u"Death Note"_s);
    }

    /// The tree is there from the moment of finding, before a single checksum is computed
    /// and before the server has answered. That is what tells a card being checked apart
    /// from an empty one.
    void the_tree_is_there_before_a_single_checksum()
    {
        m_imports->setVerifying(true);
        m_offers->offer({aFolder()});

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QVERIFY(!nodes.isEmpty());
        QCOMPARE(nodes.constFirst().toMap().value(u"name"_s).toString(),
                 u"Koro Quest"_s);
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));
    }

    /// A volume says nothing before the server has answered — not « déjà là », which is
    /// what an empty `toSend` would otherwise read as. `toSend` is empty for every row
    /// until the answer arrives, and without this guard a folder still being asked about
    /// would show every one of its volumes as already in the library.
    void a_volume_says_nothing_before_the_server_has_answered()
    {
        m_imports->setVerifying(true);
        m_offers->offer({aFolder()});
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));
        m_imports->toggle(0, QString());

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QString state = u"missing"_s;
        for (const QVariant &one : nodes) {
            const QVariantMap node = one.toMap();
            if (node.value(u"name"_s).toString() == u"Tome 1.cbz"_s)
                state = node.value(u"state"_s).toString();
        }
        QVERIFY(state.isEmpty());
    }

    /// Folded, a node does not hand over its children. That is the whole accordion: dig
    /// down to what is in doubt, and no further — a sixty-volume series unfolded by
    /// default would be a wall.
    void a_folded_node_does_not_hand_over_its_children()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_21","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        const auto nodes = [this] {
            return m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes))
                .toList();
        };
        const int folded = int(nodes().size());
        QVERIFY(nodes().constFirst().toMap().value(u"expandable"_s).toBool());
        QVERIFY(!nodes().constFirst().toMap().value(u"expanded"_s).toBool());

        m_imports->toggle(0, QString());
        QVERIFY(nodes().size() > folded);
        QVERIFY(nodes().constFirst().toMap().value(u"expanded"_s).toBool());

        m_imports->toggle(0, QString());
        QCOMPARE(int(nodes().size()), folded);
    }

    /// Manifests are built one at a time. Twelve walks over the disk at once would get in
    /// each other's way, and the first card would not be ready any sooner for it.
    void manifests_are_built_one_at_a_time()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_22","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s), aFolder(u"Trois"_s)});

        // Only one being verified, the others waiting their turn.
        QCOMPARE(m_tally->reading(), 1);
        QTRY_COMPARE(m_tally->reading(), 0);
    }

    /// The sixth case: the folder declares a series the library already holds somewhere
    /// else. It is not a creation, it is a move — and nothing moves unless somebody says so.
    void a_series_already_elsewhere_is_offered_and_never_taken()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit")) {
                return aReply(200,
                              R"({"root":"Terres d’Arran","installed":0,"orphans":[],)"
                              R"("moved":["w-1"],"open":false})");
            }
            return aReply(200,
                          R"({"id":"imp_9","root":"Terres d’Arran","creates":[],)"
                          R"("moves":[{"workId":"w-1","matched":"IDENTITY","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        // Something to move is something to ask about, exactly like something to create:
        // a series that leaves the place its reader put it is one they would have to find
        // again.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));

        const QVariantList offered = m_imports->data(m_imports->index(0, 0),
                                                     int(Imports::Role::Moves)).toList();
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered.constFirst().toMap().value(u"name"_s).toString(), u"Elfes"_s);
        // Clear until somebody ticks it. This is the assertion the whole case turns on.
        QVERIFY(!offered.constFirst().toMap().value(u"filing"_s).toBool());
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Filing, u"w-1"_s));

        m_imports->accept(0);
        // Nothing ticked and nothing to send: the card has no work at all, so starting the
        // transfer drops it and tells the server to clean its session up. The move it was
        // offering is not taken — which is what this case turns on, and is now said by the
        // card leaving rather than by a commit carrying an empty list.
        m_queue->send();
        QTRY_COMPARE(m_imports->rowCount({}), 0);
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/"),
                     m_pretend->heard.constData());
        QVERIFY2(!m_pretend->heard.contains("/commit"), m_pretend->heard.constData());
    }

    /// And ticked, the identity travels with the commit — not as a call of its own, because
    /// until the folder is installed there is no « here » to file anything under.
    void a_series_ticked_is_named_in_the_commit()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit")) {
                return aReply(200,
                              R"({"root":"Terres d’Arran","installed":0,"orphans":[],)"
                              R"("moved":["w-1"],"open":false})");
            }
            return aReply(200,
                          R"({"id":"imp_10","root":"Terres d’Arran","creates":[],)"
                          R"("moves":[{"workId":"w-1","matched":"IDENTITY","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));

        m_imports->setFiling(0, u"w-1"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Filing, u"w-1"_s));
        QVERIFY(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Moves))
                    .toList()
                    .constFirst()
                    .toMap()
                    .value(u"filing"_s)
                    .toBool());

        m_imports->accept(0);
        m_pretend->heard.clear();
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("\"move\":[\"w-1\"]"),
                 m_pretend->heard.constData());
    }

    /// A tick set is a tick that can be cleared. It was proved before by re-announcing the
    /// folder under `setComplete`, which threw every tick away; now that scope is gone, the
    /// only way to change an answer is `setFiling` itself, and it has to actually clear one.
    void a_tick_does_not_survive_being_set_false()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200,
                          R"({"id":"imp_11","root":"Terres d’Arran","creates":[],)"
                          R"("moves":[{"workId":"w-1","matched":"IDENTITY","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));
        m_imports->setFiling(0, u"w-1"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Filing, u"w-1"_s));

        m_imports->setFiling(0, u"w-1"_s, false);
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Filing, u"w-1"_s));
    }

    /// A 409 is neither a failure nor a success: the server holds less than this client
    /// believed. It says how much, and the next attempt starts exactly there.
    void an_offset_the_server_refuses_is_asked_about_rather_than_retried()
    {
        bool refusedOnce = false;
        m_pretend->answerFor = [&refusedOnce](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":1,"orphans":[],"open":false})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_4","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            if (request.startsWith("GET /import/imp_4"))
                return aReply(200, R"({"id":"imp_4","root":"Koro",)"
                                   R"("received":{"Tome 1.cbz":4},"missing":[]})");
            if (!refusedOnce) {
                refusedOnce = true;
                return aReply(409, R"({"error":"impossible offset","received":4})");
            }
            return aReply(200, R"({"path":"Tome 1.cbz","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        // It asked where to pick up rather than starting the volume again.
        QVERIFY2(m_pretend->heard.contains("GET /import/imp_4"),
                 m_pretend->heard.constData());
    }

    /// The fingerprint costs a full read of the folder, so the answer is given before
    /// anything is dropped rather than after.
    void asking_not_to_verify_leaves_the_checksums_out()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_7","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        QVERIFY(m_imports->verifying());
        m_imports->setVerifying(false);
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY2(!m_pretend->heard.contains("checksum"), m_pretend->heard.constData());
    }

    void verifying_puts_a_fingerprint_on_every_file()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_8","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY2(m_pretend->heard.contains("checksum"), m_pretend->heard.constData());
    }

    /// The count is there from the very first frame, before the walk has read a single
    /// byte — « Vérification · 0/3 tomes » rather than the bare word, which stayed fixed
    /// for thirty seconds once and made the desktop offer to kill the process. It clears
    /// once finding has moved on, because by then it is not this folder's present tense any
    /// more.
    void checking_carries_a_count_from_the_first_frame()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_60","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});

        // Read back before the event loop has had a turn: no progress call can have
        // reached this row yet, so the count is exactly zero against the three volumes
        // `Manifest::found` already saw.
        QCOMPARE(
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString(),
            Words::checking(0, 3));

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        // Past `Asking` the count is gone. This fixture answers with nothing to send, so
        // what is left is the card saying it would move no byte — which is not a count.
        const QString said =
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString();
        QCOMPARE(said, Words::nothingToSend());
        QVERIFY2(!said.contains(u'/'), "a count outliving the walk that produced it");
    }

    /// The count arrives gradually — not as a single value posted once `Manifest::of` has
    /// already returned, which shows the same two edges as the real thing and nothing in
    /// between. Proven by a mutation, because reading counted enough: moving `describe()`'s
    /// one per-file `promise.setProgressValue` call to a single call after the walk
    /// finished left the whole suite green, 36 tests over five runs — `promise.
    /// setProgressRange` reports `0/Files` on its own the moment the task starts, so even
    /// that mutant's `hashedSoFar` fires twice, at the two edges, and a first version of
    /// this test that only asked for *some* value short of the final one passed against it
    /// by mistaking that `0/Files` for one. What the mutant cannot produce is a count
    /// strictly between the two — neither the range taking hold nor the walk's own end —
    /// which is the one thing asserted below. Files large enough that a real gap separates
    /// one volume's report from the next: on three nine-byte files the whole walk fits
    /// inside one turn of the event loop, Qt's own compression of rapid progress reports
    /// can legitimately deliver only the edges either way, and the mutant and the real
    /// code become indistinguishable.
    void the_hash_walk_reports_an_intermediate_count_not_only_the_last_one()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_61","root":"Grand","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const QString at = m_folder.filePath(u"Grand"_s);
        QVERIFY(QDir().mkpath(at));
        constexpr int Files = 8;
        aDeclaration(at, "Grand");
        for (int i = 0; i < Files; ++i)
            aVolume(at + u"/Tome %1.cbz"_s.arg(i), i, 6 * 1024 * 1024);

        QStringList seen;
        const auto connection = connect(
            m_imports, &QAbstractItemModel::dataChanged, m_imports,
            [this, &seen](const QModelIndex &topLeft, const QModelIndex &,
                          const QList<int> &roles) {
                if (!roles.contains(int(Imports::Role::Checking)))
                    return;
                const QString text =
                    m_imports->data(topLeft, int(Imports::Role::Checking)).toString();
                if (!text.isEmpty())
                    seen << text;
            });

        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QObject::disconnect(connection);

        // Not a count of how many values arrived — Qt is free to compress that to as few
        // as the two edges — but whether any of them sits strictly between `0/Files` (the
        // range taking hold before a single file is read, reported by `setProgressRange`
        // itself) and `Files/Files` (the walk's own end). The mutant produces exactly
        // those two and nothing else, which is why comparing against the final string
        // alone, or against "any value that is not the final one", both let it through.
        bool sawIntermediate = false;
        for (const QString &text : seen) {
            if (text != Words::checking(0, Files) && text != Words::checking(Files, Files)) {
                sawIntermediate = true;
                break;
            }
        }
        QVERIFY2(sawIntermediate,
                 "no count strictly between the start and the final one was ever seen — "
                 "the report may have arrived only at the range's own edges");
        QCOMPARE(seen.constLast(), Words::checking(Files, Files));
    }

    /// A shelf of several series queues every one of them at `Asking`, but only one is
    /// actually on the pool. Traced, not measured — found by reading `describeNext`
    /// before it could bite: every queued folder would have shown a count frozen at zero,
    /// which reads as eleven stuck cards out of twelve rather than eleven waiting their
    /// turn. The spec's own word for that wait is « en attente » — not the bare
    /// « Vérification » a queued card said before this fix, which names a hash nobody is
    /// computing for it yet.
    void only_the_folder_actually_walking_shows_a_count()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_64","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QCOMPARE(m_imports->rowCount({}), 2);

        // The first queued is the first walked — `describeNext` takes them in order.
        QCOMPARE(
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString(),
            Words::checking(0, 3));
        // The second is genuinely waiting, not stuck: « en attente ».
        QCOMPARE(
            m_imports->data(m_imports->index(1), int(Imports::Role::Checking)).toString(),
            Words::waitingToBeChecked());

        QTRY_COMPARE(m_tally->reading(), 0);
    }

    /// A line of the choosing step counts from numbers and not from the card's sentence: queued
    /// behind another, on the pool with its volumes counted from zero, then over with every one
    /// of them read — whatever the last report said, which only ever says how far it had come.
    void a_folder_is_counted_from_queued_to_over()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_70","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const auto progress = [this](int row) {
            return m_imports->data(m_imports->index(row), int(Imports::Role::Progress)).toMap();
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});

        // Before the event loop has had a turn: the first is on the pool, the second queued.
        QCOMPARE(progress(0).value(u"state"_s).toInt(), int(Card::Counting::Walking));
        QCOMPARE(progress(0).value(u"done"_s).toInt(), 0);
        QCOMPARE(progress(0).value(u"whole"_s).toInt(), 3);
        QVERIFY(progress(0).value(u"hashing"_s).toBool());
        QCOMPARE(progress(1).value(u"state"_s).toInt(), int(Card::Counting::Waiting));
        QCOMPARE(progress(1).value(u"done"_s).toInt(), 0);

        QTRY_COMPARE(m_tally->reading(), 0);
        for (int row = 0; row < 2; ++row) {
            QCOMPARE(progress(row).value(u"state"_s).toInt(), int(Card::Counting::Over));
            QCOMPARE(progress(row).value(u"done"_s).toInt(), 3);
            QCOMPARE(progress(row).value(u"whole"_s).toInt(), 3);
        }
    }

    /// A walk that hashed and could not hash the members of an archive sent that volume as a
    /// file alone: the line says why, instead of « Vérifié » as if the pages were covered.
    void a_folder_whose_members_could_not_be_hashed_is_counted_as_uncovered()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_72","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const QString at = aFolder(u"Mêlée"_s);
        QFile broken(at + u"/Tome 4.cbz"_s);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("Ceci n’est pas un zip");
        broken.close();
        const auto progress = [this](int row) {
            return m_imports->data(m_imports->index(row), int(Imports::Role::Progress)).toMap();
        };
        m_offers->offer({at, aFolder(u"Entière"_s)});

        QTRY_COMPARE(m_tally->reading(), 0);
        QCOMPARE(progress(0).value(u"state"_s).toInt(), int(Card::Counting::Over));
        QCOMPARE(progress(0).value(u"uncovered"_s).toString(), Words::notAnArchive());
        QVERIFY(progress(1).value(u"uncovered"_s).toString().isEmpty());
    }

    /// The walk tells the model of each volume through `Checking` alone — see
    /// `Carrier::hashedSoFar` — and the line's count and the drop's total follow without that
    /// walk having to know them: a count that only moved when the folder ended was the
    /// button greyed with nothing saying why. Strictly between the two edges, for the reason
    /// the test above this one gives about the walk's own report.
    void the_count_of_a_line_and_the_total_of_the_drop_move_with_each_volume()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_71","root":"Grand","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const QString at = m_folder.filePath(u"Grand"_s);
        QVERIFY(QDir().mkpath(at));
        constexpr int Files = 8;
        aDeclaration(at, "Grand");
        for (int i = 0; i < Files; ++i)
            aVolume(at + u"/Tome %1.cbz"_s.arg(i), i, 6 * 1024 * 1024);

        QList<int> lineSaid;
        QList<qint64> totalSaid;
        const auto connection = connect(
            m_imports, &QAbstractItemModel::dataChanged, m_imports,
            [this, &lineSaid](const QModelIndex &topLeft, const QModelIndex &,
                              const QList<int> &roles) {
                if (roles.contains(int(Imports::Role::Progress)))
                    lineSaid << m_imports->data(topLeft, int(Imports::Role::Progress))
                                    .toMap().value(u"done"_s).toInt();
            });
        const auto total = connect(m_imports, &Imports::progressed, m_imports,
                                   [this, &totalSaid] { totalSaid << m_tally->volumesChecked(); });

        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QObject::disconnect(connection);
        QObject::disconnect(total);

        const auto between = [](const auto &said) {
            return std::ranges::any_of(said, [](auto one) { return one > 0 && one < Files; });
        };
        QVERIFY2(between(lineSaid), "the line's count never stood between none and all");
        QVERIFY2(between(totalSaid), "the drop's total never stood between none and all");
        QCOMPARE(m_tally->volumesToCheck(), Files);
        QCOMPARE(m_tally->volumesChecked(), Files);
    }

    /// The drop counts across every folder, and counts only what can still be read: a folder
    /// that failed holds nothing the line under the zone should wait for.
    void the_drop_counts_its_volumes_across_every_folder()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_72","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QCOMPARE(m_tally->volumesToCheck(), 6);
        QCOMPARE(m_tally->listed(), 2);
        QTRY_COMPARE(m_tally->reading(), 0);
        QCOMPARE(m_tally->volumesChecked(), 6);
    }

    /// Without a checksum the walk still opens every archive to read what it says, so it
    /// still takes time and still carries a count — under « Lecture », never under
    /// « Vérification », which names a checksum nobody is computing. The bare « En attente »
    /// it said before was right while the walk was a `stat` of each file; it is not now.
    /// Read back synchronously, before any progress can have landed: the worst case for a
    /// word that names the wrong work.
    void not_verifying_still_reads_and_says_reading()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_nv","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->setVerifying(false);
        m_offers->offer({aFolder()});

        const QString checking =
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString();
        QCOMPARE(checking, Words::reading(0, 3));
        QVERIFY2(checking != Words::checking(0, 3),
                 "said « Vérification » for a walk that computed no checksum");

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
    }

    /// A folder abandoned while its own walk is still on the pool must not corrupt
    /// whichever folder takes the slot next: `m_walkingToken` and the lookups in
    /// `described`/`hashedSoFar` are keyed on the row's own token exactly so that an
    /// answer belonging to a row that is gone finds nobody, rather than the row that
    /// happens to be walking when it finally arrives.
    void abandoning_the_walking_row_does_not_corrupt_the_next_one()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_ab","root":"x","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QCOMPARE(m_imports->rowCount({}), 2);
        // Row 0 ("Un") is the one actually walking — abandon it before its walk can
        // possibly have finished (three nine-byte files hash far faster than this call).
        m_imports->abandon(0);
        QCOMPARE(m_imports->rowCount({}), 1);

        // The remaining row ("Deux" — both share the same declared title, `aFolder()`'s
        // `work.json` always says "Koro Quest", so only the row count tells them apart
        // here) takes its turn once `describeNext` notices the queue is free, and reaches
        // `Ready` cleanly — which it could not do stuck behind a corrupted token.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        // And never shown a count belonging to the row that is gone. This fixture answers
        // with nothing to send, so what the badge carries here is the card saying it would
        // move no byte — anything holding a fraction would be the dead count.
        const QString left =
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString();
        QCOMPARE(left, Words::nothingToSend());
        QVERIFY2(!left.contains(u'/'), "a count belonging to the row that is gone");
    }

    /// A folder that cannot be read fails alone, and says why.
    void a_folder_that_cannot_be_read_says_so()
    {
        m_offers->offer({m_folder.filePath(u"absent"_s)});
        QCOMPARE(m_imports->count(), 0);
    }

    /// A malformed answer is refused and names what it could not read, rather than leaving
    /// a row that says nothing and never moves.
    void an_answer_the_client_cannot_read_fails_the_row_and_says_why()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"rcv_11"})");
        };
        m_offers->offer({aFile(u"Tome 5.cbz"_s)});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        const QString said =
            m_imports->data(m_imports->index(0), int(Imports::Role::Trouble)).toString();
        QVERIFY2(!said.isEmpty(), "a failed row that says nothing");
    }

    /// Two folders of one name in a single drop target the same library folder: `root` is
    /// the folder's name, and the server installs into `library/<root>`. The second would
    /// overwrite what the first just filed, without a word said — so it is posed anyway,
    /// and it says why it does not leave.
    void two_folders_of_one_name_cannot_both_land()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_40","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        const QString shelf = m_folder.filePath(u"Étagère"_s);
        for (const QString &under : {u"Koro"_s, u"Vieux/Koro"_s}) {
            const QString at = shelf + u'/' + under;
            [&] { QVERIFY(QDir().mkpath(at)); }();
            aDeclaration(at, "Koro");
            aVolume(at + u"/Tome 1.cbz"_s, 1);
        }

        m_offers->offer({shelf});

        QCOMPARE(m_imports->rowCount({}), 2);
        // The first departs, the second is refused and names both — found by its state,
        // not by a row: the day offer() sorts or inserts elsewhere, a fixed index would
        // point at the wrong card.
        const int refused = stageOf(0) == int(Imports::Stage::Failed) ? 0 : 1;
        QCOMPARE(stageOf(refused), int(Imports::Stage::Failed));
        const QString why =
            m_imports->data(m_imports->index(refused, 0), int(Imports::Role::Trouble)).toString();
        QVERIFY2(why.contains(u"Vieux/Koro"_s), qPrintable(why));
        QVERIFY2(why.contains(u"Koro"_s), qPrintable(why));
        // And it does not occupy the finding queue: a refused card has nothing to read.
        QTRY_COMPARE(stageOf(1 - refused), int(Imports::Stage::Ready));
    }

    /// The second folder of one name is refused before any walk, so it had no sheet, and
    /// the checking frame, which draws sheets, showed nothing of it: it carries one, a
    /// series by its folder's name, holding the reason it stays.
    void a_folder_refused_for_its_name_carries_a_sheet_that_says_why()
    {
        m_pretend->answerFor = sessions();
        const QString shelf = m_folder.filePath(u"Étagère"_s);
        for (const QString &under : {u"Koro"_s, u"Vieux/Koro"_s}) {
            const QString at = shelf + u'/' + under;
            QVERIFY(QDir().mkpath(at));
            aDeclaration(at, "Koro");
            aVolume(at + u"/Tome 1.cbz"_s, 1);
        }

        m_offers->offer({shelf});

        const int refused = stageOf(0) == int(Imports::Stage::Failed) ? 0 : 1;
        const Card &card = cardAt(refused);
        QCOMPARE(card.road.node.name, u"Koro"_s);
        QCOMPARE(card.road.sheets.size(), 1);
        const Facts::Sheet &sheet = card.road.sheets.constFirst();
        QCOMPARE(sheet.level, Manifest::Level::Work);
        QCOMPARE(sheet.at, QString());
        QCOMPARE(sheet.name, u"Koro"_s);
        QCOMPARE(sheet.sidecar, u"work.json"_s);
        QCOMPARE(sheet.trouble, card.trouble);
        QVERIFY(!sheet.trouble.isEmpty());
    }

    /// Twenty-four corrections made one by one said the queue changed twenty-four times,
    /// and every one rebuilt the checking frame: made as one batch, they say it once.
    void a_batch_of_edits_emits_once()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QSignalSpy changed(m_imports, &Imports::changed);

        const int went = m_imports->edit(QList<Imports::Edit>{
            {cardAt(0).token, QString(), u"title"_s, u"Premier"_s},
            {cardAt(1).token, QString(), u"title"_s, u"Second"_s},
        });

        QCOMPARE(went, 2);
        QCOMPARE(changed.size(), 1);
        for (int row = 0; row < 2; ++row) {
            QCOMPARE(stageOf(row), int(Imports::Stage::Reviewing));
            QVERIFY(cardAt(row).road.edited);
            QCOMPARE(cardAt(row).road.corrections.size(), 1);
        }
    }

    /// A batch is a series of single corrections and refuses what one refuses: a card whose
    /// bytes are already on their way takes none of it, and nothing is said to change.
    void a_batch_refuses_what_a_single_edit_refuses()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_1","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QSignalSpy changed(m_imports, &Imports::changed);

        QCOMPARE(m_imports->edit(QList<Imports::Edit>{
                     {cardAt(0).token, QString(), u"title"_s, u"Autre"_s}}),
                 0);
        QCOMPARE(changed.size(), 0);
        QVERIFY(cardAt(0).road.corrections.isEmpty());
        QCOMPARE(stageOf(0), int(Imports::Stage::Sending));
    }

    /// A universe declared inside a declared universe is what the server refuses at the
    /// announcement, with a 400 in English: the card was proposed ready and then failed on
    /// a sentence nobody here wrote. It is stopped before anything is sent, and says why.
    void a_declaration_out_of_place_stops_its_card_before_it_is_sent()
    {
        m_pretend->answerFor = sessions();
        const QString monde = m_folder.filePath(u"Monde"_s);
        const QString arran = monde + u"/Terres d’Arran"_s;
        for (const QString &at : {monde, arran}) {
            [&] { QVERIFY(QDir().mkpath(at)); }();
            QFile declaration(at + u"/universe.json"_s);
            [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
            declaration.write(R"({"leaf":1,"name":")" + QFileInfo(at).fileName().toUtf8()
                              + R"("})");
        }
        [&] { QVERIFY(QDir().mkpath(arran + u"/Elfes"_s)); }();
        aDeclaration(arran + u"/Elfes"_s, "Elfes");
        aVolume(arran + u"/Elfes/Tome 1.cbz"_s, 1);

        m_offers->offer({monde});

        QCOMPARE(m_imports->rowCount({}), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Failed));
        const QString why =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble)).toString();
        QCOMPARE(why, Words::declaredOutOfPlace(u"Terres d’Arran"_s, u"universe.json"_s,
                                                Words::impossibleUniverseInUniverse()));
        QVERIFY2(!m_pretend->heard.contains("POST /import"), m_pretend->heard.constData());
        // Nothing a role could change: the card is in no drop a role is chosen in.
        QCOMPARE(cardAt(0).road.drop, quint64(0));
    }

    /// Nothing goes inside an edition, an edition included: the server refuses it at the
    /// announcement, and `Bonus` declared an edition inside `Perfect` was proposed ready.
    void an_edition_declared_inside_an_edition_stops_its_card()
    {
        m_pretend->answerFor = sessions();
        const QString dragon = m_folder.filePath(u"Dragon Ball"_s);
        const QString perfect = dragon + u"/Perfect"_s;
        const QString bonus = perfect + u"/Bonus"_s;
        [&] { QVERIFY(QDir().mkpath(bonus)); }();
        aDeclaration(dragon, "Dragon Ball");
        for (const QString &at : {perfect, bonus}) {
            QFile declaration(at + u"/edition.json"_s);
            [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
            declaration.write(R"({"leaf":1,"name":")" + QFileInfo(at).fileName().toUtf8()
                              + R"("})");
            aVolume(at + u"/Tome 1.cbz"_s, 1);
        }

        m_offers->offer({dragon});

        QCOMPARE(m_imports->rowCount({}), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Failed));
        const QString why =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble)).toString();
        QCOMPARE(why, Words::declaredOutOfPlace(u"Bonus"_s, u"edition.json"_s,
                                                Words::impossibleInsideAnEdition()));
        QVERIFY2(!m_pretend->heard.contains("POST /import"), m_pretend->heard.constData());
    }

    /// A series declared under a folder that declares nothing, inside a declared universe,
    /// is what the server accepts — it compares a declaration with the nearest one above
    /// it — and the card goes: stopped here for the folder between, it was refused by
    /// nobody but this client.
    void a_folder_declaring_nothing_between_two_declarations_does_not_stop_its_card()
    {
        m_pretend->answerFor = sessions();
        const QString univers = m_folder.filePath(u"Univers"_s);
        const QString serie = univers + u"/Cycle 1/Série"_s;
        [&] { QVERIFY(QDir().mkpath(serie)); }();
        QFile declaration(univers + u"/universe.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"name":"Univers"})");
        declaration.close();
        aDeclaration(serie, "Série");
        aVolume(serie + u"/Tome 1.cbz"_s, 1);

        m_offers->offer({univers});

        QCOMPARE(m_imports->rowCount({}), 1);
        QVERIFY(stageOf(0) != int(Imports::Stage::Failed));
        QTRY_VERIFY2(m_pretend->heard.contains("POST /import"), m_pretend->heard.constData());
    }

    /// The tree follows the transfer: the volume in flight is the one moving, the ones
    /// before it are filed, the ones after are still waiting. Without this the tree is a
    /// memory of what was going to happen, and a volume that arrived wrong shows up nowhere.
    void the_tree_says_where_each_volume_is()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit")) {
                return aReply(200, R"({"root":"Koro","installed":3,"orphans":[],)"
                                   R"("open":false})");
            }
            if (request.startsWith("POST /import")) {
                return aReply(200, R"({"id":"imp_30","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":27})");
            }
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        const auto stateOf = [this](const QString &name) {
            const QVariantList nodes =
                m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
            for (const QVariant &one : nodes) {
                const QVariantMap node = one.toMap();
                if (node.value(u"name"_s).toString() == name)
                    return node.value(u"state"_s).toString();
            }
            return u"missing"_s;
        };

        // Before it starts, all three are waiting.
        QCOMPARE(stateOf(u"Tome 1.cbz"_s), Words::willBeSent());
        QCOMPARE(stateOf(u"Tome 3.cbz"_s), Words::willBeSent());

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        // Everything has gone, so everything is filed — the only state that is still true
        // afterwards.
        QCOMPARE(stateOf(u"Tome 1.cbz"_s), Words::wasFiled(Manifest::Level::Volume));
        QCOMPARE(stateOf(u"Tome 3.cbz"_s), Words::wasFiled(Manifest::Level::Volume));
    }

    /// A volume whose transfer failed says so, and not « à envoyer » — the row's own
    /// `trouble` already names why the queue stopped, but the node itself kept saying
    /// something untrue at the exact moment somebody looked at it to find out what failed.
    void a_volume_whose_transfer_failed_says_so()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import")) {
                return aReply(200, R"({"id":"imp_33","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":27})");
            }
            if (request.startsWith("PUT /import"))
                return aReply(403, R"({"error":"this key does not carry the import right"})");
            return aReply(200, R"({})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QString state = u"missing"_s;
        for (const QVariant &one : nodes) {
            const QVariantMap node = one.toMap();
            if (node.value(u"name"_s).toString() == u"Tome 1.cbz"_s)
                state = node.value(u"state"_s).toString();
        }
        QCOMPARE(state, Words::failedToSend());
    }

    /// A volume the server already holds says so, and does not say "to send" — it is the
    /// only thing that tells a folder half already there from a brand new one.
    void a_volume_the_server_already_holds_says_so()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_31","root":"Koro","creates":[],)"
                               R"("toSend":["Tome 2.cbz"],)"
                               R"("alreadyThere":["Tome 1.cbz","Tome 3.cbz"],)"
                               R"("bytesToSend":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QMap<QString, QString> said;
        for (const QVariant &one : nodes)
            said.insert(one.toMap().value(u"name"_s).toString(),
                        one.toMap().value(u"state"_s).toString());
        QCOMPARE(said.value(u"Tome 1.cbz"_s), Words::alreadyInTheLibrary());
        QCOMPARE(said.value(u"Tome 2.cbz"_s), Words::willBeSent());
        QCOMPARE(said.value(u"Tome 3.cbz"_s), Words::alreadyInTheLibrary());
    }

    /// A volume the library holds under this very path, which the arriving one does not
    /// match, warns instead of describing. It is in `toSend` like any other — a commit
    /// still sends it — and saying « à envoyer » over it would be the one word wrong at
    /// the one moment it is read: just before somebody accepts.
    void a_volume_that_would_be_overwritten_warns_rather_than_saying_it_is_on_its_way()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_33","root":"Koro","creates":[],)"
                               R"("replaces":[{"path":"Tome 1.cbz","size":9,)"
                               R"("presentSize":11,"presentTitle":"Le vieux titre",)"
                               R"("presentRead":true}],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":18})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QMap<QString, QString> said;
        for (const QVariant &one : nodes)
            said.insert(one.toMap().value(u"name"_s).toString(),
                        one.toMap().value(u"state"_s).toString());
        QCOMPARE(said.value(u"Tome 1.cbz"_s), Words::willBeReplaced(Manifest::Level::Volume));
        QCOMPARE(said.value(u"Tome 2.cbz"_s), Words::willBeSent());
    }

    /// A declaration the library already holds is written over only if somebody says so.
    ///
    /// `replace`'s twin one floor up. A series every volume of which the library already
    /// holds still carries its `work.json`, so a commit installed it over a title or a
    /// summary edited through the API — and the card said « déjà là » while it happened.
    void a_declaration_the_library_holds_is_rewritten_only_when_it_is_ticked()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":0,"orphans":[],"open":false})");
            if (request.startsWith("POST /import")) {
                return aReply(200, R"({"id":"imp_36","root":"Koro","creates":[],)"
                                   R"("replaces":[],)"
                                   R"("declarations":[{"path":"work.json","present":{},)"
                                   R"("presentName":"Koro Quest","differs":["summary"]}],)"
                                   R"("toSend":[],"alreadyThere":["Tome 1.cbz"],)"
                                   R"("bytesToSend":0})");
            }
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        const QVariantMap root = nodes.constFirst().toMap();
        QCOMPARE(root.value(u"redeclarable"_s).toBool(), true);
        QCOMPARE(root.value(u"declaring"_s).toBool(), false);
        QVERIFY(root.value(u"present"_s).toString().contains(u"Koro Quest"_s));
        QVERIFY(root.value(u"present"_s).toString().contains(u"summary"_s));

        m_imports->setDeclaring(0, u"work.json"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Declaring, u"work.json"_s));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("\"declare\":[\"work.json\"]"),
                 m_pretend->heard.constData());
    }

    /// A folder read to its last byte says so, and does not go back to looking queued.
    ///
    /// Between the walk ending and the server answering, a card is waiting on neither the
    /// disk nor its turn — and it said « En attente », which is what a card that has not
    /// started says, and what an announced one says too. Three moments under one word left
    /// a reader with no way to know a verification had finished.
    void a_folder_read_to_the_end_says_so_rather_than_looking_queued()
    {
        // The server never answers, so the card stays between the two for the whole test.
        m_pretend->answerFor = [](const QByteArray &) { return QByteArray(); };
        m_offers->offer({aFolder()});

        const auto checking = [this] {
            return m_imports->data(m_imports->index(0), int(Imports::Role::Checking))
                .toString();
        };
        QTRY_COMPARE(checking(), Words::announcing());
        QVERIFY(checking() != Words::waitingToBeChecked());
    }

    /// Six volumes that could be replaced are six decisions, and the commit carries only
    /// the ones that were ticked.
    ///
    /// `move`'s twin, and for the same reason its own comment gives: overwriting a volume
    /// somebody already has is a decision, and a commit must never take it on its own. The
    /// boxes start clear — dropping a folder on a library is not a request to overwrite it.
    void only_the_volumes_that_were_ticked_are_offered_for_replacement()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":1,"orphans":[],"open":false})");
            if (request.startsWith("POST /import")) {
                return aReply(200, R"({"id":"imp_34","root":"Koro","creates":[],)"
                                   R"("replaces":[{"path":"Tome 1.cbz","size":9,)"
                                   R"("presentSize":11,"presentTitle":"Le vieux titre",)"
                                   R"("presentRead":true},)"
                                   R"({"path":"Tome 2.cbz","size":9,"presentSize":12,)"
                                   R"("presentRead":false}],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":18})");
            }
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        // Clear by default: nothing is overwritten by having been dropped.
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 2.cbz"_s));

        m_imports->setReplacing(0, u"Tome 1.cbz"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 2.cbz"_s));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        const QByteArray heard = m_pretend->heard;
        QVERIFY2(heard.contains("\"replace\":[\"Tome 1.cbz\"]"), heard.constData());
    }

    /// What the server says a reimport finds, with the volumes and the rest typed by the
    /// test; one session per announcement, numbered in the order they come.
    std::function<QByteArray(const QByteArray &)> reimportAnswering(const QByteArray &volumes,
                                                                    const QByteArray &extra = {})
    {
        return [this, volumes, extra](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":1,"orphans":[],"open":false})");
            if (request.startsWith("POST /import ")) {
                const QByteArray id = "imp_r" + QByteArray::number(++m_opened);
                return aReply(200, R"({"id":")" + id + R"(","root":"Koro","creates":[],)"
                                   R"("volumes":[)" + volumes + R"(],)" + extra
                                       + R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                         R"("bytesToSend":9})");
            }
            return aReply(200, R"({"path":"x","received":9})");
        };
    }

    static QByteArray aMatch(const QByteArray &path, const QByteArray &outcome)
    {
        return "{\"path\":\"" + path + "\",\"lands\":\"" + path + "\",\"outcome\":\""
               + outcome + "\",\"arriving\":{}}";
    }

    /// A reimport is stopped for its reader to check, says what it aims at and says, file by
    /// file, what the file declares: the server matches on the number and the kind and
    /// nothing else, so they are the ones the person saw.
    void a_reimport_announces_its_target_and_what_each_file_declares()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 1.cbz", "REPLACE"));
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QJsonObject body = announcementOf(u"Koro"_s);
        QCOMPARE(body.value(u"target"_s).toObject().value(u"kind"_s).toString(), u"SERIES"_s);
        QCOMPARE(body.value(u"target"_s).toObject().value(u"id"_s).toString(), u"ed-1"_s);
        const QJsonObject found = body.value(u"files"_s).toArray().first().toObject()
                                      .value(u"found"_s).toObject();
        QCOMPARE(found.value(u"number"_s).toDouble(), 1.0);
        QCOMPARE(found.value(u"kind"_s).toString(), u"VOLUME"_s);
        QCOMPARE(found.value(u"pages"_s).toInt(), 1);
        QVERIFY(!found.contains(u"guessed"_s));
    }

    /// Adding editions aims the drop at the series with its own kind, so the server files each
    /// folder as an edition and not as volumes of the one held; and cancelling creates nothing.
    void adding_editions_aims_the_drop_at_the_series_and_cancelling_creates_nothing()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 1.cbz", "ADD"));
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QJsonObject body = announcementOf(u"Koro"_s);
        QCOMPARE(body.value(u"target"_s).toObject().value(u"kind"_s).toString(),
                 u"SERIES_NEW_EDITIONS"_s);
        QCOMPARE(body.value(u"target"_s).toObject().value(u"id"_s).toString(), u"ed-1"_s);

        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 0);
        QVERIFY(!m_pretend->heard.contains("/commit"));
    }

    static QByteArray namesAsked(const QByteArray &problems)
    {
        return R"("editionsNeedingAName":[{"path":"@current","current":true},)"
               R"({"path":"Deluxe","current":false}],"problems":[)"
               + problems + "],";
    }

    /// An edition the server asks a name for is a row with no name and a problem, and it holds
    /// the card back: sending would be refused, nothing installed.
    void a_new_edition_without_a_name_is_red_and_blocks_sending()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            namesAsked(R"({"path":"@current","kind":"EMPTY"},{"path":"Deluxe","kind":"EMPTY"})"));
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const Card &card = m_imports->cards().constFirst();
        QVERIFY(Carrier::heldBack(card));
        const QVariantList rows = Aimed::editionRows(card);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(u"label"_s).toString(), Words::currentEdition());
        QCOMPARE(rows.at(0).toMap().value(u"problem"_s).toString(), Words::editionNameMissing());
        QCOMPARE(rows.at(1).toMap().value(u"label"_s).toString(), u"Deluxe"_s);
    }

    /// A name that is another's is said with whose, and it is the server that says it.
    void two_editions_of_the_same_name_are_red()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            namesAsked(R"({"path":"Deluxe","kind":"DUPLICATE","with":"Originale"})"));
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QVariantList rows = Aimed::editionRows(m_imports->cards().constFirst());
        QCOMPARE(rows.at(1).toMap().value(u"problem"_s).toString(),
                 Words::editionNameTaken(u"Originale"_s));
    }

    /// A typed name goes where the server reads one: into the `edition.json` of its folder,
    /// and into `currentEditionName` for the edition the series holds; the folder is
    /// announced again with them.
    void a_typed_edition_name_travels_with_the_announcement()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            namesAsked(R"({"path":"@current","kind":"EMPTY"},{"path":"Deluxe","kind":"EMPTY"})"));
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const quint64 token = m_imports->cards().constFirst().token;

        QVERIFY(m_imports->setEditionName(token, u"@current"_s, u"Originale"_s));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->setEditionName(token, u"Deluxe"_s, u"Deluxe"_s));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QJsonObject body = announcementOf(u"Koro"_s);
        QCOMPARE(body.value(u"currentEditionName"_s).toString(), u"Originale"_s);
        bool written = false;
        for (const QJsonValue &one : body.value(u"sidecars"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"Deluxe/edition.json"_s) {
                written = QJsonDocument::fromJson(one.toObject().value(u"json"_s).toString().toUtf8())
                              .object().value(u"name"_s).toString() == u"Deluxe"_s;
            }
        }
        QVERIFY2(written, "the name is in Deluxe/edition.json");
        QVERIFY2(!m_imports->setEditionName(token, u"Deluxe"_s, u"Deluxe"_s),
                 "the same name again asks nothing");
    }

    /// A name accepted is no longer asked for by the server, and its row stays: a typo is
    /// corrected where it was typed.
    void an_accepted_name_keeps_its_row()
    {
        m_pretend->answerFor = [this](const QByteArray &request) {
            if (request.startsWith("POST /import ") && request.contains("currentEditionName")) {
                return reimportAnswering(aMatch("Tome 1.cbz", "ADD"))(request);
            }
            return reimportAnswering(aMatch("Tome 1.cbz", "ADD"),
                                     namesAsked(R"({"path":"@current","kind":"EMPTY"})"))(request);
        };
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(Aimed::editionRows(m_imports->cards().constFirst()).size(), 2);

        QVERIFY(m_imports->setEditionName(m_imports->cards().constFirst().token, u"@current"_s,
                                          u"Originale"_s));
        QTRY_VERIFY(m_imports->cards().constFirst().road.problems.isEmpty());
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QVariantList rows = Aimed::editionRows(m_imports->cards().constFirst());
        QVERIFY(!rows.isEmpty());
        QCOMPARE(rows.constFirst().toMap().value(u"name"_s).toString(), u"Originale"_s);
    }

    /// A name typed while the answer is awaited is kept, and the folder announced again with
    /// it once the answer is in: nothing the reader typed is refused or lost.
    void a_name_typed_while_the_answer_is_awaited_is_announced_after_it()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            namesAsked(R"({"path":"@current","kind":"EMPTY"},{"path":"Deluxe","kind":"EMPTY"})"));
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));

        QVERIFY(m_imports->setEditionName(m_imports->cards().constFirst().token, u"@current"_s,
                                          u"Originale"_s));

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QTRY_COMPARE(announcementOf(u"Koro"_s).value(u"currentEditionName"_s).toString(),
                     u"Originale"_s);
    }

    /// What adding an edition does to the volumes already held is said on the card, with two
    /// of the new names as examples.
    void adding_editions_says_what_it_reshelves_and_how_the_new_volumes_are_called()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            R"("reshelved":[{"edition":"Koro Quest › Originale","count":42}],)"
            R"("landsAs":[{"path":"Deluxe/a.cbz","folder":"Koro Quest › Deluxe",)"
            R"("file":"koro-quest_deluxe_tome_001.cbz","wasCalled":"a.cbz"}],)");
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QString said = Aimed::note(m_imports->cards().constFirst());

        QVERIFY2(said.contains(Words::reshelved(42, u"Koro Quest › Originale"_s)),
                 qPrintable(said));
        QVERIFY2(said.contains(u"a.cbz → koro-quest_deluxe_tome_001.cbz"_s), qPrintable(said));
    }

    /// Files dropped loose form an edition whose name is asked at the root (`path` empty): the
    /// `edition.json` made for it has no folder to sit in and still has to reach the server.
    void a_name_typed_for_loose_files_reaches_the_server()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            R"("editionsNeedingAName":[{"path":"","current":false}],"problems":[{"path":"","kind":"EMPTY"}],)");
        m_offers->reimport(u"SERIES_NEW_EDITIONS"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(m_imports->setEditionName(m_imports->cards().constFirst().token, QString(),
                                          u"Couleur"_s));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        bool written = false;
        for (const QJsonValue &one : announcementOf(u"Koro"_s).value(u"sidecars"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"edition.json"_s)
                written = true;
        }
        QVERIFY2(written, "the root edition.json is sent");
    }

    /// A file aimed at a volume goes by the road of a folder: `/preflight` would ask the
    /// server which series it belongs to, and the reader has just said.
    void a_single_file_aimed_at_a_volume_travels_as_a_folder_of_one()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 2.cbz", "REPLACE"));
        const QString file = aFolder() + u"/Tome 2.cbz"_s;
        m_offers->reimport(u"ENTRY"_s, u"en-2"_s, u"Tome 2"_s, {file});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(!m_pretend->heard.contains("/intake"));
        const QJsonObject body = announcementOf(u"Tome 2"_s);
        QCOMPARE(filesOf(body), QStringList({u"Tome 2.cbz"_s}));
        QCOMPARE(body.value(u"target"_s).toObject().value(u"kind"_s).toString(), u"ENTRY"_s);
        QCOMPARE(body.value(u"files"_s).toArray().first().toObject().value(u"found"_s)
                     .toObject().value(u"number"_s).toDouble(), 2.0);
    }

    /// Aimed at something, a replacement is what was asked for: what matched a held volume
    /// and what differs in a declaration start ticked. A file that is another series never
    /// does — it would land on a volume of something else.
    void a_reimport_ticks_its_replacements_and_never_another_series()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "REPLACE") + QByteArrayLiteral(",") + aMatch("Tome 2.cbz", "UPDATE") + ','
                + aMatch("Tome 3.cbz", "OTHER_SERIES") + QByteArrayLiteral(",") + aMatch("Tome 4.cbz", "ADD"),
            R"("declarations":[{"path":"work.json","differs":["publisher"],)"
            R"("present":{"publisher":"Pika"}}],)");
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        using enum Imports::Choice;
        QVERIFY(m_imports->ticked(0, Replacing, u"Tome 1.cbz"_s));
        QVERIFY(m_imports->ticked(0, Replacing, u"Tome 2.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Replacing, u"Tome 3.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Replacing, u"Tome 4.cbz"_s));
        QVERIFY(m_imports->ticked(0, Declaring, u"work.json"_s));
    }

    /// Non-regression: dropping a folder again on a library is not a request to overwrite
    /// it, whatever the server could match.
    void a_drop_without_target_ticks_nothing()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "REPLACE"),
            R"("declarations":[{"path":"work.json","differs":["publisher"],"present":{}}],)");
        m_offers->offer({aFolder()});

        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        using enum Imports::Choice;
        QVERIFY(!m_imports->ticked(0, Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Declaring, u"work.json"_s));
        QVERIFY(!announcementOf(u"Koro"_s).contains(u"target"_s));
    }

    /// The aimed volume replaced anyway is a number corrected: typing 2 over the 1 the file
    /// declared drops the session and announces the file again as volume 2.
    void replacing_the_aimed_volume_anyway_is_a_number_corrected_and_announced_again()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 1.cbz", "OTHER_NUMBER"));
        m_offers->reimport(u"ENTRY"_s, u"en-2"_s, u"Tome 2"_s, {aFolder() + u"/Tome 1.cbz"_s});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(m_imports->edit(cardAt(0).token, u"Tome 1.cbz"_s, u"number"_s, 2));
        m_queue->confirm();

        QTRY_VERIFY(m_pretend->heard.contains("DELETE /import/imp_r1"));
        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        const QJsonObject again = bodiesOf("POST /import ").last();
        QCOMPARE(again.value(u"files"_s).toArray().first().toObject().value(u"found"_s)
                     .toObject().value(u"number"_s).toDouble(), 2.0);
    }

    /// A value kept is the one the library holds, sent in place of the drop's — and one the
    /// library does not hold is removed, since « non renseigné » is not an empty value.
    void keeping_a_value_sends_the_one_the_library_holds()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "REPLACE"),
            R"("declarations":[{"path":"work.json","differs":["readingDirection","title"],)"
            R"("present":{"readingDirection":"LEFT_TO_RIGHT"}}],)");
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Koro Quest"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(m_imports->keep(cardAt(0).token, u"work.json"_s, u"readingDirection"_s, true));
        QVERIFY(m_imports->keep(cardAt(0).token, u"work.json"_s, u"title"_s, true));
        // Nothing the library holds under a sidecar the answer never named.
        QVERIFY(!m_imports->keep(cardAt(0).token, u"edition.json"_s, u"title"_s, true));
        m_queue->confirm();

        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        const QJsonObject again = bodiesOf("POST /import ").last();
        QJsonObject sent;
        for (const QJsonValue &one : again.value(u"sidecars"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"work.json"_s)
                sent = QJsonDocument::fromJson(
                           one.toObject().value(u"json"_s).toString().toUtf8()).object();
        }
        QCOMPARE(sent.value(u"readingDirection"_s).toString(), u"LEFT_TO_RIGHT"_s);
        QVERIFY(!sent.contains(u"title"_s));
    }

    /// Starting a place again is a decision about one profile's reading: it goes with the
    /// commit of the card it was ticked on, and a card nobody ticked asks for nothing.
    void a_reset_travels_with_the_commit_and_only_when_asked()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 1.cbz", "REPLACE"));
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Koro Quest"_s,
                            {aFolder(u"Koro"_s), aFolder(u"Koro 2"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Reviewing));

        QVERIFY(m_imports->setResetting(cardAt(0).token, u"Tome 1.cbz"_s, true));
        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Filed));

        QCOMPARE(bodiesOf("POST /import/imp_r1/commit").constFirst().value(u"reset"_s).toArray(),
                 QJsonArray({u"Tome 1.cbz"_s}));
        QVERIFY(!bodiesOf("POST /import/imp_r2/commit").constFirst().contains(u"reset"_s));
    }

    /// A series named like one the library holds, aimed at that one instead: the session is
    /// dropped and the folder announced onto it, from the walk already made.
    void retargeting_a_resembling_series_announces_it_onto_that_series()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "ADD"),
            R"("resembles":[{"at":"","seriesId":"ed-7","name":"Koro quest","from":"BD"}],)");
        m_offers->reimport(u"UNIVERSE"_s, u"un-1"_s, u"Monde"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        // Only the card's own root can be aimed elsewhere.
        QVERIFY(!m_imports->retarget(cardAt(0).token, u"Sous"_s, u"SERIES"_s, u"ed-7"_s));
        QVERIFY(m_imports->retarget(cardAt(0).token, QString(), u"SERIES"_s, u"ed-7"_s));

        QTRY_VERIFY(m_pretend->heard.contains("DELETE /import/imp_r1"));
        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        const QJsonObject target = bodiesOf("POST /import ").last().value(u"target"_s).toObject();
        QCOMPARE(target.value(u"kind"_s).toString(), u"SERIES"_s);
        QCOMPARE(target.value(u"id"_s).toString(), u"ed-7"_s);
        QCOMPARE(cardAt(0).road.target->name, u"Koro quest"_s);
    }

    /// The pages that replace a chapter are a correction like a number: announced again at
    /// « Importer », and not a byte of the file goes up before that.
    void a_splice_changed_before_sending_is_announced_again_and_nothing_is_uploaded()
    {
        m_pretend->answerFor = reimportAnswering(aMatch("Tome 1.cbz", "REPLACE"));
        m_offers->reimport(u"CHAPTER"_s, u"ch-1"_s, u"Chapitre 4"_s,
                            {aFolder() + u"/Tome 1.cbz"_s});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QJsonObject first = bodiesOf("POST /import ").constFirst().value(u"files"_s)
                                      .toArray().first().toObject().value(u"splice"_s).toObject();
        QCOMPARE(first.value(u"from"_s).toInt(), 0);

        QVERIFY(m_imports->setSplice(cardAt(0).token, 1, 5));
        // Not on a splice that is the one already there, nor on one backwards.
        QVERIFY(!m_imports->setSplice(cardAt(0).token, 1, 5));
        QVERIFY(!m_imports->setSplice(cardAt(0).token, 4, 2));
        QVERIFY(!m_pretend->heard.contains("PUT "));
        QCOMPARE(bodiesOf("POST /import ").size(), qsizetype(1));

        m_queue->confirm();
        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        const QJsonObject again = bodiesOf("POST /import ").last().value(u"files"_s).toArray()
                                      .first().toObject().value(u"splice"_s).toObject();
        QCOMPARE(again.value(u"from"_s).toInt(), 1);
        QCOMPARE(again.value(u"to"_s).toInt(), 5);
    }

    /// The work.json of the folder as the announcement carried it, parsed.
    QJsonObject declarationAnnounced(const QJsonObject &body) const
    {
        for (const QJsonValue &one : body.value(u"sidecars"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"work.json"_s)
                return QJsonDocument::fromJson(one.toObject().value(u"json"_s).toString().toUtf8())
                    .object();
        }
        return {};
    }

    /// A series the universe holds elsewhere is known by its title alone, which two series can
    /// share: the move is made by the identity the declaration it travels with carries, so
    /// ticking it announces the folder again with that identity written in.
    void moving_a_series_matched_by_title_writes_its_identity_and_ticks_the_move()
    {
        m_pretend->answerFor = reimportAnswering(
            aMatch("Tome 1.cbz", "SAME"),
            R"("target":{"kind":"UNIVERSE","id":"u1","name":"Koro","trail":[)"
            R"({"kind":"UNIVERSE","id":"u1","name":"Koro","count":1}]},)"
            R"("moves":[{"workId":"w-koro","name":"Koro Quest","from":"","at":"",)"
            R"("matched":"TITLE"}],)");
        m_offers->reimport(u"UNIVERSE"_s, u"u1"_s, u"Koro"_s, {aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(!declarationAnnounced(bodiesOf("POST /import ").constLast()).contains(u"id"_s));

        m_imports->setFiling(0, u"w-koro"_s, true);
        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        QCOMPARE(declarationAnnounced(bodiesOf("POST /import ").constLast())
                     .value(u"id"_s).toString(),
                 u"w-koro"_s);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(cardAt(0).road.filing.contains(u"w-koro"_s));
    }

    /// A folder whose title is a series the library holds, met without a target, stops to be
    /// asked about; reimported onto the one held, it announces that target and creates
    /// nothing.
    void a_resembling_series_reimported_onto_the_one_held_creates_nothing()
    {
        m_pretend->answerFor = [this](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (!request.startsWith("POST /import "))
                return QByteArray();
            const bool onto = request.contains("\"target\"");
            return aReply(200, R"({"id":"imp_s)" + QByteArray::number(++m_opened)
                                   + R"(","root":"Koro","creates":)"
                                   + (onto ? QByteArray("[]")
                                           : QByteArray(R"([{"kind":"WORK","name":"Koro Quest",)"
                                                        R"("at":""}])"))
                                   + R"(,"resembles":)"
                                   + (onto ? QByteArray("[]")
                                           : QByteArray(R"([{"at":"","seriesId":"s1",)"
                                                        R"("name":"Koro quest","from":""}])"))
                                   + R"(,"volumes":[],"toSend":[],"alreadyThere":[],)"
                                     R"("bytesToSend":0)"
                                   + (onto ? QByteArray(R"(,"target":{"kind":"SERIES","id":"s1",)"
                                                        R"("name":"Koro quest","trail":[]})")
                                           : QByteArray())
                                   + "}");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(cardAt(0).road.resembles.size(), 1);

        QVERIFY(m_imports->retarget(cardAt(0).token, QString(), u"SERIES"_s, u"s1"_s));
        QTRY_COMPARE(bodiesOf("POST /import ").size(), qsizetype(2));
        const QJsonObject again = bodiesOf("POST /import ").constLast();
        QCOMPARE(again.value(u"target"_s).toObject().value(u"id"_s).toString(), u"s1"_s);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(cardAt(0).road.creates.isEmpty());
    }

    /// What the library already holds is said on the line, so a reader is not left to guess
    /// what six hundred bytes of difference are made of.
    void a_volume_that_would_be_overwritten_says_what_is_already_there()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_35","root":"Koro","creates":[],)"
                               R"("replaces":[{"path":"Tome 1.cbz","size":9,)"
                               R"("presentSize":11,"presentTitle":"Le vieux titre",)"
                               R"("presentRead":true},)"
                               R"({"path":"Tome 2.cbz","size":9,"presentSize":12,)"
                               R"("presentRead":false}],)"
                               R"("toSend":["Tome 1.cbz","Tome 2.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":18})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QMap<QString, QVariantMap> said;
        for (const QVariant &one : nodes)
            said.insert(one.toMap().value(u"name"_s).toString(), one.toMap());

        QCOMPARE(said.value(u"Tome 1.cbz"_s).value(u"replaceable"_s).toBool(), true);
        QVERIFY(said.value(u"Tome 1.cbz"_s).value(u"present"_s).toString().contains(
            u"Le vieux titre"_s));
        // The server did not open the second one, and the line says that rather than
        // letting a missing title read as an archive that declares none.
        QVERIFY(said.value(u"Tome 2.cbz"_s).value(u"present"_s).toString().contains(
            u"non relu"_s));
        // And the series above them stops saying nothing is going to happen.
        QCOMPARE(nodes.constFirst().toMap().value(u"state"_s).toString(),
                 Words::holdsReplacements(2, 0));

        // And ticking one changes what the series says it will do — which is the whole
        // point of the box, and was invisible while the line counted what *could* happen.
        m_imports->setReplacing(0, u"Tome 1.cbz"_s, true);
        const QVariantList after =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QCOMPARE(after.constFirst().toMap().value(u"state"_s).toString(),
                 Words::holdsReplacements(2, 1));
    }

    /// And a container says what the server announced for it, by its path and not by its
    /// name — two editions of one work can carry the same declared name.
    void a_container_says_what_the_server_announced_for_it()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_32","root":"Koro","creates":)"
                               R"([{"kind":"WORK","name":"Koro Quest","at":""}],)"
                               R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_offers->offer({aFolder()});
        // `Ready` and no longer `Deciding`: a creation does not stop a card, and the line
        // read here is what stands in for the question it used to ask.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QCOMPARE(nodes.constFirst().toMap().value(u"state"_s).toString(),
                 Words::willBeCreated(Manifest::Level::Work));
    }

    /// A chunk that went moves the send's counters at once: they were told by `changed` alone
    /// and stood still on « 0 tome sur 76 » until a pause, while the ring beside them moved.
    void a_chunk_that_went_tells_the_counters_of_the_send()
    {
        m_offers->offer({aFolder()});
        QCOMPARE(m_imports->count(), 1);
        QSignalSpy told(m_imports, &Imports::progressed);
        emit m_imports->dataChanged(m_imports->index(0, 0), m_imports->index(0, 0),
                                    {int(Imports::Role::Sent)});
        QCOMPARE(told.count(), 1);
    }

    /// The names every `.qml` file binds to, and the roles a card actually reads back.
    ///
    /// `roleNames()` is the whole contract between this model and the screen, and it is a
    /// contract nothing enforces: a role added to the enum and forgotten here, or renamed
    /// here and not in the delegate, is a binding that evaluates to `undefined` — no
    /// warning, no failure, a card with a blank where the word was. The same silence
    /// `client_knows_the_contract.py` exists to break one layer up, one layer down.
    void the_model_answers_to_the_names_the_screen_binds_to()
    {
        const QHash<int, QByteArray> named = m_imports->roleNames();
        const QList<QByteArray> expected{"name",     "stage",      "sent",     "size",
                                         "reason",   "confidence", "candidates", "concerns",
                                         "chosen",   "trouble",    "retryIn",  "folder",
                                         "creates",  "moves",      "nodes",    "checking", "readHow",
                                         "progress", "volumes",    "bytes",    "about",
                                         "alarm",    "fraction",   "released", "shown"};
        QCOMPARE(named.size(), expected.size());
        for (const QByteArray &one : expected) {
            QVERIFY2(named.key(one, -1) != -1,
                     QByteArray("no role answers to " + one).constData());
        }

        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200,
                          R"({"id":"imp_roles","root":"Koro","creates":)"
                          R"([{"kind":"WORK","name":"Koro Quest","at":""}],)"
                          R"("toSend":["Tome 1.cbz"],"alreadyThere":[],"bytesToSend":9,)"
                          R"("moves":[{"workId":"w-1","matched":"IDENTITY","name":"Koro Quest","from":"M/Koro",)"
                          R"("at":""}]})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));

        // Each one read the way a delegate reads it — by the name, through the model — and
        // not by calling the method behind it. `creates` and `moves` are the two that had a
        // `Q_INVOKABLE` of their own standing in for them in every other test here, so the
        // case that answers the role was the one thing nothing went through.
        const QModelIndex card = m_imports->index(0, 0);
        const QList<QByteArray> drawn{"name", "stage", "size", "folder",
                                      "creates", "moves", "nodes"};
        for (const QByteArray &one : drawn) {
            QVERIFY2(m_imports->data(card, named.key(one)).isValid(), one.constData());
        }
        QCOMPARE(m_imports->data(card, named.key("creates")).toList().size(), 1);
        QCOMPARE(m_imports->data(card, named.key("moves")).toList().size(), 1);
        QVERIFY(m_imports->data(card, named.key("folder")).toBool());
    }

    /// Every answer a screen can ask for, about a row that is not there.
    ///
    /// A delegate outlives the row it draws. Abandoning a card while a click is on its way
    /// hands this model an index one past the end, and nothing in QML notices — the row is
    /// gone from the list and the handler still carries the number it had. Each of these
    /// guards is the difference between that click doing nothing and it reading past the
    /// end of `m_rows`.
    void a_row_that_is_not_there_is_answered_rather_than_reached_into()
    {
        for (const int row : {-1, 0, 7}) {
            m_imports->decide(row, u"ed-1"_s);
            m_imports->accept(row);
            m_imports->toggle(row, QString());
            m_queue->pause(row);
            m_queue->resume(row);
            m_imports->abandon(row);
            m_imports->setFiling(row, u"w-1"_s, true);
            m_imports->setReplacing(row, u"Tome 1.cbz"_s, true);
            m_imports->setDeclaring(row, u"work.json"_s, true);
            QVERIFY(!m_imports->ticked(row, Imports::Choice::Filing, u"w-1"_s));
            QVERIFY(!m_imports->ticked(row, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
            QVERIFY(!m_imports->ticked(row, Imports::Choice::Declaring, u"work.json"_s));
            QVERIFY(!m_imports->data(m_imports->index(row, 0), int(Imports::Role::Name))
                         .isValid());
        }
        QCOMPARE(m_imports->count(), 0);
    }

    /// A transfer that fails waits, says for how long, and comes back on its own.
    ///
    /// Not the same thing as a refusal: a 500 or a cut cable is nobody's decision, and the
    /// file has nothing wrong with it. The seconds are shown because a queue that retries
    /// in silence is a queue that looks stuck.
    ///
    /// Written last, and it found two things. `pump()` reached past the countdown: a row
    /// `retryLater` had just parked was the only one left, so the loop that hands the slot
    /// back to a paused file took it back in the same turn — fail, resume, fail, as fast as
    /// the answers came. And `attempts` was raised before the table was read, so the first
    /// wait was the table's *second* entry and the two seconds it opens with were dead
    /// code. Both are why this asserts the number and not only the stage.
    void a_transfer_that_fails_waits_out_loud_and_starts_again_by_itself()
    {
        int refusals = 0;
        m_pretend->answerFor = [&refusals](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_9");
            if (request.startsWith("PUT /intake") && refusals++ == 0)
                return aReply(500, R"({"error":"le disque"})");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"Tome 9.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-9","path":"x","replacement":false})");
        };
        m_offers->offer({aFile(u"Tome 9.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Paused));
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::RetryIn)).toInt(),
                 2);

        // Two seconds later, without anybody clicking anything.
        QTRY_COMPARE_WITH_TIMEOUT(stageOf(0), int(Imports::Stage::Filed), 10000);
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::RetryIn)).toInt(),
                 0);
    }

    /// Giving up throws away what is still being set up, and tells the server about each.
    ///
    /// What the × and « Revenir en arrière » mean. The server keeps what nobody named — a
    /// place reserved and then forgotten holds the library's own disk for good — so the
    /// cleanup is the point, not the row disappearing.
    void giving_up_clears_the_queue_and_frees_what_was_held_for_it()
    {
        int given = 0;
        m_pretend->answerFor = [&given](const QByteArray &request) {
            if (request.startsWith("DELETE /intake/"))
                ++given;
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_" + QByteArray::number(given));
            return aReply(204, QByteArray());
        };
        m_offers->offer({aFile(u"Tome 5.cbz"_s), aFile(u"Tome 6.cbz"_s)});
        QTRY_COMPARE(m_imports->count(), 2);
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));

        m_queue->giveUpPreparing();

        QCOMPARE(m_imports->count(), 0);
        QTRY_COMPARE(given, 2);
    }

    /// A universe dropped whole is one card with its series under it, and the tree says
    /// what becomes of each — before the commit and after it.
    ///
    /// Everything here is a recursion the flat folders above never reach: the tree is
    /// rebased through its children, the containers a volume sits in are read back out of
    /// its path, and a state is asked two levels down. And the past tense is the one
    /// `stateOfNode`'s own comment was written for — the tree kept promising « sera créée »
    /// under a badge that already read « Envoyé », seventy-six volumes into a library that
    /// already held them.
    void a_universe_says_what_becomes_of_each_series_under_it()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Terres","installed":1,"orphans":[],)"
                                   R"("open":false})");
            if (request.startsWith("POST /import"))
                return aReply(200,
                              R"({"id":"imp_u","root":"Terres",)"
                              R"("creates":[{"kind":"UNIVERSE","name":"Terres","at":""},)"
                              R"({"kind":"WORK","name":"Elfes","at":"Elfes"}],)"
                              R"("moves":[{"workId":"w-2","matched":"IDENTITY","name":"Mages",)"
                              R"("from":"Mangas/Mages","at":"Mages"}],)"
                              R"("toSend":["Elfes/Tome 1.cbz"],)"
                              R"("alreadyThere":["Mages/Tome 1.cbz"],"bytesToSend":9})");
            return aReply(200, R"({"path":"Elfes/Tome 1.cbz","received":9})");
        };
        m_offers->offer({aUniverse()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));
        m_imports->toggle(0, QString());

        const auto stateOf = [this](const QString &at) {
            const QVariantList nodes =
                m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
            for (const QVariant &one : nodes) {
                if (one.toMap().value(u"at"_s).toString() == at)
                    return one.toMap().value(u"state"_s).toString();
            }
            return QStringLiteral("no such node");
        };

        using enum Manifest::Level;
        QCOMPARE(stateOf(QString()), Words::willBeCreated(Universe));
        QCOMPARE(stateOf(u"Elfes"_s), Words::willBeCreated(Work));
        // The one the server said it already holds elsewhere: dropping the universe here
        // would move it, and the line says so before anybody has agreed to anything.
        QCOMPARE(stateOf(u"Mages"_s), Words::willBeMoved(Work));

        m_imports->accept(0);
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));

        QCOMPARE(stateOf(QString()), Words::wasCreated(Universe));
        QCOMPARE(stateOf(u"Mages"_s), Words::wasMoved(Work));
    }

    /// Unticking a replacement and a declaration puts each back where it started.
    ///
    /// The twin of `a_tick_does_not_survive_being_set_false`, one box per floor. Both
    /// default to clear and both are cleared again by every announcement — a box that
    /// could be set and not unset is a decision a reader cannot take back, and the two
    /// that overwrite something are exactly the two they would want to.
    void a_replacement_and_a_declaration_can_both_be_untold()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200,
                          R"({"id":"imp_u2","root":"Koro","creates":[],)"
                          R"("toSend":["Tome 1.cbz"],"alreadyThere":[],"bytesToSend":9,)"
                          R"("replaces":[{"path":"Tome 1.cbz","size":9,"presentSize":8,)"
                          R"("presentTitle":"Koro","presentRead":false}],)"
                          R"("declarations":[{"path":"work.json","present":{},"presentName":"Koro",)"
                          R"("differs":["titre"]}]})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->setReplacing(0, u"Tome 1.cbz"_s, true);
        m_imports->setDeclaring(0, u"work.json"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(m_imports->ticked(0, Imports::Choice::Declaring, u"work.json"_s));

        m_imports->setReplacing(0, u"Tome 1.cbz"_s, false);
        m_imports->setDeclaring(0, u"work.json"_s, false);
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Declaring, u"work.json"_s));
    }

    /// Every way the server can refuse, and the one card each refusal belongs to.
    ///
    /// A refusal is one file's own fact. Only a key that lost its right to import stops the
    /// whole queue — everything else has to come back as this row failed, carrying the
    /// sentence the server sent, because a queue that halts on one bad volume is a queue
    /// nobody can empty. Four refusals, one per point on the two roads where the server
    /// gets to say no.
    void a_refusal_fails_the_one_card_it_belongs_to_and_keeps_its_sentence()
    {
        // The pre-flight, on a file's road.
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(500, R"({"error":"le disque est plein"})");
        };
        m_offers->offer({aFile(u"Tome 20.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY2(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble))
                     .toString()
                     .contains(u"disque"_s),
                 "the server's own sentence is what the card shows");
        QVERIFY(m_imports->trouble().isEmpty());

        // The announcement, on a folder's.
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(500, R"({"error":"l’index est verrouillé"})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Failed));
        QVERIFY(m_imports->trouble().isEmpty());
    }

    /// A commit the server refuses fails the card after every byte has gone up.
    ///
    /// The last thing that can go wrong, and the worst one to swallow: the volumes are on
    /// the server, the rename did not happen, and a card reading « Rangé » over that would
    /// send a reader looking for them in a library that does not hold them.
    void a_commit_the_server_refuses_fails_the_card_and_never_says_it_landed()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(500, R"({"error":"le renommage a échoué"})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_c","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return aReply(200, R"({"path":"Tome 1.cbz","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY2(m_pretend->heard.contains("POST /import/imp_c/commit"),
                 m_pretend->heard.constData());
    }

    /// A commit whose answer this client cannot read fails the same way.
    ///
    /// Not the same as a refusal: the server said yes and said it in a shape `Api::installed`
    /// will not accept. Reading it as an empty answer would put « 0 tome envoyé » on a card
    /// whose volumes did land.
    void a_commit_answered_in_a_shape_this_client_cannot_read_fails_the_card()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro"})");
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_d","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return aReply(200, R"({"path":"Tome 1.cbz","received":9})");
        };
        m_offers->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY2(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble))
                     .toString()
                     .contains(u"installed"_s),
                 "the card names the answer it could not read");
    }

    /// A volume that went away between the walk and the transfer says which one.
    ///
    /// The walk reads the folder once and the transfer reads it again, minutes later on a
    /// real series. Anything can happen in between — a file moved, a disk unplugged — and
    /// the card has to name the volume rather than fail with a number.
    void a_volume_gone_between_the_walk_and_the_transfer_is_named()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_g","root":"Koro","creates":[],)"
                                   R"("toSend":["Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return aReply(200, R"({"path":"Tome 1.cbz","received":9})");
        };
        const QString at = aFolder();
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY(QFile::remove(at + u"/Tome 1.cbz"_s));
        m_queue->send();

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble)).toString(),
                 Words::couldNotBeRead(u"Tome 1.cbz"_s));
    }

    /// The list of what to send comes from the server, and a path in it is read from the
    /// disk. One that climbs out of the dropped folder is a file this client was never shown:
    /// it is refused, whatever its size would have come to — it used to send nothing only
    /// because the size of a path the walk never met is nought.
    void a_path_the_walk_never_met_is_never_read_from_the_disk()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import"))
                return aReply(200, R"({"id":"imp_u","root":"Koro","creates":[],)"
                                   R"("toSend":["../outside.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return aReply(200, R"({"path":"../outside.cbz","received":9})");
        };
        const QString at = aFolder();
        QFile outside(QFileInfo(at).absolutePath() + u"/outside.cbz"_s);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("a secret");
        outside.close();
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_queue->send();

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY(!m_pretend->heard.contains("PUT /import"));
        QVERIFY(!m_pretend->heard.contains("a secret"));
    }

    /// A shelf whose folders say nothing used to give one card per folder of archives —
    /// `Manifest::found` reads declarations only — so a universe of two series was two cards
    /// filed under no universe, and a series in two editions two series. The shape reads it
    /// as three things, and the drop is kept for the roles to change later.
    void dropping_a_shelf_of_unsaid_folders_makes_one_card_per_reading()
    {
        m_pretend->answerFor = sessions();
        const QString bd = aShelfOfUnsaidFolders();
        m_offers->offer({bd});

        QCOMPARE(m_imports->count(), 3);
        QStringList names{nameOf(0), nameOf(1), nameOf(2)};
        names.sort();
        QCOMPARE(names, (QStringList{u"Dragon Ball"_s, u"Terres d'Arran"_s, u"Vrac"_s}));
        const quint64 drop = cardAt(0).road.drop;
        QVERIFY(drop != 0);
        const Shape::Drop *kept = m_imports->dropOf(drop);
        QVERIFY(kept);
        QCOMPARE(kept->path, bd);
        for (int row = 0; row < 3; ++row) {
            QCOMPARE(cardAt(row).road.drop, drop);
            QCOMPARE(cardAt(row).road.within, nameOf(row));
            QCOMPARE(cardAt(row).path, bd + u'/' + nameOf(row));
        }

        QTRY_COMPARE(m_imports->reviewing(), 3);
        // Two roles guessed and one series with no title: three readings to confirm.
        QCOMPARE(m_imports->readings(), 3);
    }

    /// What the shape guessed is written, or the scan reads a shelf again on the next pass:
    /// it reads a role from a sidecar and never from a shape.
    void the_announcement_writes_the_roles_it_guessed()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);

        QCOMPARE(sidecarsOf(announcementOf(u"Dragon Ball"_s)),
                 (QStringList{u"Original/edition.json"_s, u"Perfect/edition.json"_s,
                              u"work.json"_s}));
        QCOMPARE(sidecarsOf(announcementOf(u"Terres d'Arran"_s)),
                 (QStringList{u"Elfes/work.json"_s, u"Mages/work.json"_s, u"universe.json"_s}));
    }

    /// A role changed inside one card reads its sheets again and waits for « Importer », like
    /// a correction: sent at once, every click on the menu would be one more session opened
    /// and forgotten.
    void a_role_changed_inside_a_card_is_announced_on_confirm()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const int row = rowNamed(u"Dragon Ball"_s);
        const QString id = cardAt(row).id;
        QVERIFY(!id.isEmpty());

        m_pretend->heard.clear();
        QVERIFY(m_imports->setRole(cardAt(row).road.drop, u"Dragon Ball"_s,
                                   Shape::Role::Universe));
        QTest::qWait(100);
        QVERIFY2(m_pretend->heard.isEmpty(), m_pretend->heard.constData());
        QCOMPARE(m_imports->count(), 3);
        QCOMPARE(stageOf(row), int(Imports::Stage::Reviewing));
        QVERIFY(cardAt(row).road.edited);
        QCOMPARE(cardAt(row).road.node.level, Manifest::Level::Universe);

        sayWhatIsLacking(row);
        m_queue->confirm();
        QTRY_VERIFY2(!announcementOf(u"Dragon Ball"_s).isEmpty(), m_pretend->heard.constData());
        const qsizetype dropped = m_pretend->heard.indexOf("DELETE /import/" + id.toUtf8());
        QVERIFY2(dropped >= 0, m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.indexOf("POST /import ") > dropped,
                 m_pretend->heard.constData());
        QCOMPARE(sidecarsOf(announcementOf(u"Dragon Ball"_s)),
                 (QStringList{u"Original/work.json"_s, u"Perfect/work.json"_s,
                              u"universe.json"_s}));
    }

    /// The other reading is a role change like any other, on the whole folder at once: the
    /// card's sheets follow it, and a reading the folder does not have changes nothing.
    void choosing_the_other_reading_reads_the_card_again()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const int row = rowNamed(u"Dragon Ball"_s);
        const quint64 drop = cardAt(row).road.drop;

        QVERIFY(!m_imports->setReading(drop, u"Dragon Ball"_s, 2));
        QVERIFY(!cardAt(row).road.edited);
        QVERIFY(m_imports->setReading(drop, u"Dragon Ball"_s, 1));
        QCOMPARE(m_imports->count(), 3);
        QCOMPARE(cardAt(row).road.node.level, Manifest::Level::Universe);
        QCOMPARE(cardAt(row).road.roles.value(u"Perfect"_s).role, Shape::Role::Work);
        bool perfectIsASeries = false;
        for (const Facts::Sheet &sheet : cardAt(row).road.sheets)
            perfectIsASeries |= sheet.at == u"Perfect"_s && sheet.level == Manifest::Level::Work;
        QVERIFY(perfectIsASeries);
        QVERIFY(cardAt(row).road.edited);
    }

    /// A value typed before the role changed is the reader's, and still means something as
    /// long as its field exists: read again without it, the number typed on a volume would
    /// silently go back to the one guessed from its name.
    void a_correction_survives_a_role_change()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const int row = rowNamed(u"Dragon Ball"_s);
        const quint64 token = cardAt(row).token;
        QVERIFY(m_imports->edit(token, u"Perfect/Tome 1.cbz"_s, u"number"_s, 7));
        QVERIFY(m_imports->edit(token, QString(), u"title"_s, u"DB"_s));
        QCOMPARE(cardAt(row).road.corrections.size(), 2);

        QVERIFY(m_imports->setRole(cardAt(row).road.drop, u"Dragon Ball"_s,
                                   Shape::Role::Universe));
        const Facts::Field number = fieldOf(row, u"Perfect/Tome 1.cbz"_s, u"number"_s);
        QCOMPARE(number.value.toDouble(), 7.0);
        QVERIFY(number.edited);
        // A universe has a name and no title: that correction names nothing any more.
        QVERIFY(fieldOf(row, QString(), u"title"_s).key.isEmpty());
        QCOMPARE(cardAt(row).road.corrections.size(), 1);
        sayWhatIsLacking(row);

        m_pretend->heard.clear();
        m_queue->confirm();
        QTRY_VERIFY2(!announcementOf(u"Dragon Ball"_s).isEmpty(), m_pretend->heard.constData());
        QJsonObject rewrite;
        for (const QJsonValue &one :
             announcementOf(u"Dragon Ball"_s).value(u"files"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"Perfect/Tome 1.cbz"_s)
                rewrite = one.toObject().value(u"rewrite"_s).toObject();
        }
        const QJsonObject entry =
            QJsonDocument::fromJson(rewrite.value(u"entryJson"_s).toString().toUtf8()).object();
        QCOMPARE(entry.value(u"number"_s).toDouble(), 7.0);
    }

    /// A shelf turned universe is one card where there were two, and stitching two walks and
    /// two sessions into one would carry over ticks given against answers that no longer
    /// exist. The old sessions are dropped, the folder read again once, and nothing of the
    /// old cards leaks into the new one.
    void turning_a_shelf_into_a_universe_regroups_its_cards()
    {
        m_pretend->answerFor = sessions();
        const QString mangas = m_folder.filePath(u"Mangas"_s);
        for (const QString &series : {u"Bleach"_s, u"Naruto"_s}) {
            QVERIFY(QDir().mkpath(mangas + u'/' + series));
            aDeclaration(mangas + u'/' + series, series.toUtf8());
            aVolume(mangas + u'/' + series + u"/Tome 1.cbz"_s, 1);
        }
        m_offers->offer({mangas});
        QCOMPARE(m_imports->count(), 2);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        const int bleach = rowNamed(u"Bleach"_s);
        m_imports->setReplacing(bleach, u"Tome 1.cbz"_s, true);
        QVERIFY(m_imports->edit(cardAt(bleach).token, QString(), u"title"_s, u"Autre"_s));
        const QStringList ids{cardAt(0).id, cardAt(1).id};
        const QList<quint64> tokens{cardAt(0).token, cardAt(1).token};
        const quint64 drop = cardAt(0).road.drop;

        QVERIFY(m_imports->setRole(drop, QString(), Shape::Role::Universe));
        QCOMPARE(m_imports->count(), 1);
        QCOMPARE(nameOf(0), u"Mangas"_s);
        QCOMPARE(cardAt(0).road.drop, drop);
        QVERIFY(cardAt(0).road.within.isEmpty());
        // A card of its own, not one of the old two re-rooted: a token and a session neither
        // of them had.
        QVERIFY(!tokens.contains(cardAt(0).token));
        for (const QString &id : ids) {
            QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/" + id.toUtf8()),
                         m_pretend->heard.constData());
        }
        // Ready, and not stopped: its name read off the folder is shown, not asked about.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QVERIFY(!ids.contains(cardAt(0).id));
        // What reaches the server is the disk's « Bleach », not the « Autre » typed on the old
        // card — and the tick given there is not in the new card's commit.
        const Facts::Field title = fieldOf(0, u"Bleach"_s, u"title"_s);
        QCOMPARE(title.value.toString(), u"Bleach"_s);
        QVERIFY(!title.edited);
        QString bleachJson;
        for (const QJsonValue &one :
             announcementOf(u"Mangas"_s).value(u"sidecars"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"Bleach/work.json"_s)
                bleachJson = one.toObject().value(u"json"_s).toString();
        }
        QVERIFY2(bleachJson.contains(u"\"Bleach\""_s) && !bleachJson.contains(u"Autre"_s),
                 qPrintable(bleachJson));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Bleach/Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QCOMPARE(sidecarsOf(announcementOf(u"Mangas"_s)),
                 (QStringList{u"Bleach/work.json"_s, u"Naruto/work.json"_s,
                              u"universe.json"_s}));
        // Read once: one walk, one announcement.
        QTest::qWait(100);
        int announced = 0;
        for (const QJsonObject &body : bodiesOf("POST /import ")) {
            if (body.value(u"root"_s).toString() == u"Mangas"_s)
                ++announced;
        }
        QCOMPARE(announced, 1);
    }

    /// Once a card has gone, a role changed in its folder could only reach the library by a
    /// second upload nobody asked for: the change is refused whole, as a late correction is.
    void a_role_change_is_refused_once_a_card_sends()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_go","root":"Dragon Ball","creates":[],)"
                                   R"("toSend":["Perfect/Tome 1.cbz"],"alreadyThere":[],)"
                                   R"("bytesToSend":9})");
            return QByteArray();
        };
        const QString alone = m_folder.filePath(u"Seule/Dragon Ball"_s);
        bareVolumes(alone + u"/Perfect"_s, 1, 3);
        bareVolumes(alone + u"/Original"_s, 1, 3);
        m_offers->offer({alone});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        sayWhatIsLacking(0);
        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        // What the corrections dropped and announced again is behind: only the refusal counts.
        m_pretend->heard.clear();

        const Shape::Roles before = cardAt(0).road.roles;
        QVERIFY(!m_imports->setRole(cardAt(0).road.drop, QString(), Shape::Role::Universe));
        QCOMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(cardAt(0).road.roles.value(QString()).role, Shape::Role::Work);
        QCOMPARE(cardAt(0).road.roles.size(), before.size());
        QCOMPARE(m_imports->dropOf(cardAt(0).road.drop)->roles.value(QString()).role,
                 Shape::Role::Work);
        QVERIFY(!m_pretend->heard.contains("DELETE"));
    }

    /// `visit_books_of` reads books and nothing else at a universe's root: an archive sent
    /// there would sit on the library's disk read by nobody. It is left out of the
    /// announcement, and it is the one reading left to confirm.
    void an_archive_left_without_role_is_not_announced()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithAnArchiveAtItsRoot()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QCOMPARE(filesOf(announcementOf(u"Monde d'Aquilon"_s)), QStringList{u"Elfes/Tome 1.cbz"_s});
        QVERIFY(!m_pretend->heard.contains("scan_final"));
        QCOMPARE(m_imports->readings(), 1);
    }

    /// Said a book, the archive travels with the `oneshot.json` that makes it one: without
    /// it the scan would put it in `disregarded` again.
    void a_book_chosen_travels_with_its_oneshot_json()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithAnArchiveAtItsRoot()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(m_imports->setRole(cardAt(0).road.drop, u"scan_final.cbz"_s,
                                   Shape::Role::Book));
        sayWhatIsLacking(0);
        m_pretend->heard.clear();
        m_queue->confirm();
        QTRY_VERIFY2(!announcementOf(u"Monde d'Aquilon"_s).isEmpty(),
                     m_pretend->heard.constData());
        const QJsonObject body = announcementOf(u"Monde d'Aquilon"_s);
        QCOMPARE(filesOf(body), (QStringList{u"Elfes/Tome 1.cbz"_s, u"scan_final.cbz"_s}));
        QJsonObject rewrite;
        for (const QJsonValue &one : body.value(u"files"_s).toArray()) {
            if (one.toObject().value(u"path"_s).toString() == u"scan_final.cbz"_s)
                rewrite = one.toObject().value(u"rewrite"_s).toObject();
        }
        const QJsonObject book =
            QJsonDocument::fromJson(rewrite.value(u"oneShotJson"_s).toString().toUtf8())
                .object();
        QCOMPARE(book.value(u"leaf"_s).toInt(), 1);
        QVERIFY2(book.value(u"title"_s).isString(),
                 qPrintable(rewrite.value(u"oneShotJson"_s).toString()));
    }

    /// A library that says everything about itself is read by its sidecars, as it was before
    /// any shape was read: the same cards, and an announcement carrying its own bytes.
    void a_drop_said_in_full_behaves_as_before()
    {
        m_pretend->answerFor = sessions();
        const QString terres = aUniverse();
        const QString koro = aFolder();
        m_offers->offer({terres, koro});
        QCOMPARE(m_imports->count(), 2);
        QCOMPARE(nameOf(0), u"Terres d’Arran"_s);
        QCOMPARE(nameOf(1), u"Koro Quest"_s);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(m_imports->readings(), 0);

        const auto disk = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        for (const auto &[root, folder] :
             {std::pair{u"Terres"_s, terres}, std::pair{u"Koro"_s, koro}}) {
            const QJsonObject body = announcementOf(root);
            QVERIFY2(!body.isEmpty(), qPrintable(root));
            for (const QJsonValue &one : body.value(u"sidecars"_s).toArray()) {
                const QJsonObject sidecar = one.toObject();
                QCOMPARE(sidecar.value(u"json"_s).toString(),
                         disk(folder + u'/' + sidecar.value(u"path"_s).toString()));
            }
            for (const QJsonValue &one : body.value(u"files"_s).toArray())
                QVERIFY(!one.toObject().contains(u"rewrite"_s));
        }
        QCOMPARE(sidecarsOf(announcementOf(u"Terres"_s)),
                 (QStringList{u"Elfes/work.json"_s, u"Mages/work.json"_s, u"universe.json"_s}));
        QCOMPARE(sidecarsOf(announcementOf(u"Koro"_s)), QStringList{u"work.json"_s});
    }

    /// Two folders of archives that say nothing, under a folder whose name is no shelf's, are
    /// one universe to confirm — not the two series `Manifest::found` made of them — and
    /// saying it is a shelf brings the two cards back.
    void two_unsaid_series_under_a_named_folder_are_one_universe_until_said_a_shelf()
    {
        m_pretend->answerFor = sessions();
        const QString arrivage = m_folder.filePath(u"Arrivage"_s);
        bareVolumes(arrivage + u"/Death Note"_s, 1, 1);
        bareVolumes(arrivage + u"/Bleach"_s, 1, 1);
        m_offers->offer({arrivage});
        QCOMPARE(m_imports->count(), 1);
        QCOMPARE(nameOf(0), u"Arrivage"_s);
        QCOMPARE(cardAt(0).road.roles.value(QString()).role, Shape::Role::Universe);
        QCOMPARE(cardAt(0).road.roles.value(QString()).origin, Shape::Origin::Guessed);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->readings() >= 1);

        QVERIFY(m_imports->setRole(cardAt(0).road.drop, QString(), Shape::Role::Shelf));
        QCOMPARE(m_imports->count(), 2);
        QStringList names{nameOf(0), nameOf(1)};
        names.sort();
        QCOMPARE(names, (QStringList{u"Bleach"_s, u"Death Note"_s}));
    }

    /// A card still being read, or announced again, has no sheets the change could be read
    /// into yet — or a session about to be replaced: the change is refused.
    void a_role_change_is_refused_while_its_card_is_read_or_announced_again()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        const int row = rowNamed(u"Dragon Ball"_s);
        const quint64 drop = cardAt(row).road.drop;
        QCOMPARE(stageOf(row), int(Imports::Stage::Asking));
        QVERIFY(!m_imports->setRole(drop, u"Dragon Ball"_s, Shape::Role::Universe));
        QTRY_COMPARE(m_imports->reviewing(), 3);

        sayWhatIsLacking(rowNamed(u"Dragon Ball"_s));
        // The new announcement held back: the card stays `Asking`, its old session dropped.
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("POST /import ");
        };
        const int ready = rowNamed(u"Dragon Ball"_s);
        const QByteArray id = cardAt(ready).id.toUtf8();
        QVERIFY(m_imports->edit(cardAt(ready).token, u"Perfect/Tome 1.cbz"_s, u"number"_s, 7));
        m_queue->confirm();
        QTRY_VERIFY2(m_pretend->heard.contains("DELETE /import/" + id),
                     m_pretend->heard.constData());
        const int again = rowNamed(u"Dragon Ball"_s);
        QCOMPARE(stageOf(again), int(Imports::Stage::Asking));
        QVERIFY(!m_imports->setRole(drop, u"Dragon Ball"_s, Shape::Role::Universe));
        QCOMPARE(cardAt(again).road.roles.value(QString()).role, Shape::Role::Work);
    }

    /// Only the cards a change touches are asked whether they can still be corrected: one
    /// still being announced elsewhere in the drop has no say in it.
    void a_card_in_flight_does_not_hold_back_a_change_elsewhere()
    {
        m_pretend->answerFor = sessions();
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("POST /import ") && request.contains("\"root\":\"Vrac\"");
        };
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 2);
        QCOMPARE(stageOf(rowNamed(u"Vrac"_s)), int(Imports::Stage::Asking));

        QVERIFY(m_imports->setRole(cardAt(0).road.drop, u"Dragon Ball"_s,
                                   Shape::Role::Universe));
        QCOMPARE(cardAt(rowNamed(u"Dragon Ball"_s)).road.node.level, Manifest::Level::Universe);
    }

    /// A card that failed is no reading of its drop any more: nothing can correct it, and
    /// counted among the drop's cards it refused every role changed beside it.
    void a_card_that_failed_leaves_its_drop()
    {
        m_pretend->answerFor = sessions();
        const QString bd = aShelfOfUnsaidFolders();
        m_offers->offer({bd});
        // Gone before its turn on the pool comes: the walk over it fails.
        QVERIFY(QDir(bd + u"/Vrac"_s).removeRecursively());
        QTRY_COMPARE(stageOf(rowNamed(u"Vrac"_s)), int(Imports::Stage::Failed));
        QTRY_COMPARE(m_imports->reviewing(), 2);
        QCOMPARE(cardAt(rowNamed(u"Vrac"_s)).road.drop, quint64(0));

        const quint64 drop = cardAt(rowNamed(u"Dragon Ball"_s)).road.drop;
        QVERIFY(m_imports->setRole(drop, u"Dragon Ball"_s, Shape::Role::Universe));
    }

    /// And a drop of which only failed cards remain is let go, like one whose cards were all
    /// abandoned.
    void a_drop_left_with_failed_cards_only_is_forgotten()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(400, R"({"error":"refused"})");
            return QByteArray();
        };
        m_offers->offer({aKomgaFolder(u"Refusée"_s)});
        const quint64 drop = cardAt(0).road.drop;
        QVERIFY(m_imports->dropOf(drop));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY(!m_imports->dropOf(drop));
    }

    /// A regroup whose old sessions the server would not drop leaves their failed rows
    /// behind, at the paths the next regroup needs: reading the folder again there must not
    /// be taken for « already a card », or the series would vanish without a word.
    void a_failed_row_does_not_stand_in_for_the_card_a_regroup_brings_back()
    {
        const auto opened = sessions();
        m_pretend->answerFor = [opened](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(500, R"({"error":"internal error"})");
            return opened(request);
        };
        const QString mangas = m_folder.filePath(u"Mangas"_s);
        for (const QString &series : {u"Bleach"_s, u"Naruto"_s}) {
            QVERIFY(QDir().mkpath(mangas + u'/' + series));
            aDeclaration(mangas + u'/' + series, series.toUtf8());
            aVolume(mangas + u'/' + series + u"/Tome 1.cbz"_s, 1);
        }
        m_offers->offer({mangas});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        const quint64 drop = cardAt(0).road.drop;

        QVERIFY(m_imports->setRole(drop, QString(), Shape::Role::Universe));
        // The two refusals come back as failed rows at the series' own paths.
        QTRY_COMPARE(m_imports->count(), 3);
        const auto liveAt = [this, drop](const QString &path) {
            return std::ranges::any_of(m_imports->cards(), [&](const Card &card) {
                return card.path == path && card.road.drop == drop
                       && card.stage != Imports::Stage::Failed;
            });
        };
        QVERIFY(liveAt(mangas));
        QTRY_COMPARE(stageOf(rowNamed(u"Mangas"_s)), int(Imports::Stage::Ready));

        QVERIFY(m_imports->setRole(drop, QString(), Shape::Role::Shelf));
        QVERIFY(liveAt(mangas + u"/Bleach"_s));
        QVERIFY(liveAt(mangas + u"/Naruto"_s));
    }

    /// A reading the reader chose asks nothing any more — `Facts::needsReview` does not stop
    /// on it either — so it leaves the count of readings still to confirm.
    void a_chosen_role_is_no_reading_left_to_confirm()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithAnArchiveAtItsRoot()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(m_imports->readings(), 1);

        QVERIFY(m_imports->setRole(cardAt(0).road.drop, u"scan_final.cbz"_s,
                                   Shape::Role::Unread));
        QCOMPARE(m_imports->readings(), 0);
    }

    /// « Vrac » is no title: imported in one press, the library would have a series called
    /// « Vrac ». Its title is missing until somebody types one, and then the card goes.
    void a_series_with_a_name_to_give_waits_for_one()
    {
        m_pretend->answerFor = sessions();
        // Two series that say all but their numbers, which go as they are, and « Vrac », which
        // nobody can call anything: it waits, with its writer, its drawer and its direction
        // said, for the one thing left.
        const QString vrac = m_folder.filePath(u"Vrac"_s);
        bareVolumes(vrac, 1, 2);
        m_offers->offer({aGuessedFolder(u"Monster"_s), aGuessedFolder(u"Pluto"_s), vrac});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        QCOMPARE(fieldOf(rowNamed(u"Vrac"_s), QString(), u"title"_s).origin,
                 Facts::Origin::Missing);

        sayWhatIsLacking(rowNamed(u"Vrac"_s));
        m_queue->confirm();
        QTRY_COMPARE(m_imports->count(), 1);
        QCOMPARE(nameOf(0), u"Vrac"_s);
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QCOMPARE(m_imports->readings(), 1);

        QVERIFY(m_imports->edit(cardAt(0).token, QString(), u"title"_s, u"Divers"_s));
        QCOMPARE(m_imports->readings(), 0);
        m_pretend->heard.clear();
        m_queue->confirm();
        QTRY_VERIFY2(!announcementOf(u"Vrac"_s).isEmpty(), m_pretend->heard.constData());
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Sending)
                    || stageOf(0) == int(Imports::Stage::Filing));
        const QJsonArray sidecars = announcementOf(u"Vrac"_s).value(u"sidecars"_s).toArray();
        QVERIFY(!sidecars.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(
                     sidecars.at(0).toObject().value(u"json"_s).toString().toUtf8())
                     .object()
                     .value(u"title"_s)
                     .toString(),
                 u"Divers"_s);
    }

    /// The drop is kept for its roles to change, and only while a card of it is there:
    /// kept after its last card went, every drop of a session would stay in memory, whole
    /// tree and all, for nothing that could ever reach it.
    void abandoning_the_last_card_of_a_drop_forgets_the_drop()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const quint64 drop = cardAt(0).road.drop;

        m_imports->abandon(2);
        m_imports->abandon(1);
        QVERIFY(m_imports->dropOf(drop));
        m_imports->abandon(0);
        QVERIFY(!m_imports->dropOf(drop));
        QVERIFY(!m_imports->setRole(drop, u"Vrac"_s, Shape::Role::Work));
    }
};

QTEST_MAIN(HoldsTheImports)
#include "holds_the_imports.moc"
