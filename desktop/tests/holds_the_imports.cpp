// The queue, and the rules nobody clicks.
//
// Most of this file is about one sentence of the spec: *pause yields a place, it does not
// stop*. There is exactly one slot, pausing hands it to the next in line, resuming takes it
// back, and a file that stepped aside starts again by itself when the one ahead is done.
// None of that is visible from a screen — it is the model's whole job, and it is why the
// model is tested here rather than through a dialog.

#include "Api.h"
#include "Cbz.h"
#include "Imports.h"
#include "Manifest.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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

    /// A folder with its declaration and three volumes, one of which the server will say
    /// it already has.
    QString aFolder(const QString &name = u"Koro"_s)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QFile declaration(at + u"/work.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"title":"Koro Quest"})");
        declaration.close();
        for (const QString &volume : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s}) {
            QFile file(at + u'/' + volume);
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(QByteArray(9, 'x'));
        }
        return at;
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
        declaration.write(R"({"leaf":1,"title":"Terres d’Arran"})");
        declaration.close();
        for (const QString &series : {u"Elfes"_s, u"Mages"_s}) {
            [&] { QVERIFY(QDir().mkpath(at + u'/' + series)); }();
            QFile said(at + u'/' + series + u"/work.json"_s);
            [&] { QVERIFY(said.open(QIODevice::WriteOnly)); }();
            said.write(R"({"leaf":1,"title":")" + series.toUtf8() + R"("})");
            said.close();
            QFile volume(at + u'/' + series + u"/Tome 1.cbz"_s);
            [&] { QVERIFY(volume.open(QIODevice::WriteOnly)); }();
            volume.write(QByteArray(9, 'x'));
        }
        return at;
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
        m_imports->offer({aFile(u"Tome 1.cbz"_s)});

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
        m_imports->offer({aFile(u"scan.cbz"_s)});

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
        m_imports->offer({path, path});
        m_imports->offer({path});

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
        m_imports->offer({aFile(u"Tome 3.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        QVERIFY2(m_pretend->heard.contains("PUT /intake/rcv_4/file"),
                 m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("POST /intake/rcv_4/file"),
                 m_pretend->heard.constData());
        QCOMPARE(m_imports->inFlight(), 0);
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
        m_imports->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s), aFile(u"C.cbz"_s)});
        QTRY_COMPARE(stageOf(2), int(Imports::Stage::Ready));

        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(stageOf(2), int(Imports::Stage::Ready));
        QVERIFY(m_imports->busy());
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
        m_imports->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(nameOf(0), u"A.cbz"_s);

        m_imports->pause(0);
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
        m_imports->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_imports->pause(0);
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Sending));

        // A is still at row 0, paused. Resuming it takes the slot back, and again nothing
        // changes place.
        m_imports->resume(0);
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(nameOf(0), u"A.cbz"_s);
        QCOMPARE(nameOf(1), u"B.cbz"_s);
        QCOMPARE(stageOf(1), int(Imports::Stage::Paused));
    }

    /// The one rule nothing clicks: a file that stepped aside starts again by itself once
    /// the one ahead of it is done.
    void a_paused_file_starts_again_when_the_one_ahead_finishes()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_8");
            if (request.startsWith("PUT /intake"))
                return aReply(200, R"({"path":"B.cbz","received":9})");
            return aReply(200, R"({"entryId":"en-2","path":"x/B.cbz","replacement":false})");
        };
        m_imports->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_imports->pause(0);
        // B runs to the end, and nobody clicks anything for A.
        QTRY_COMPARE(m_imports->inFlight(), 0);
        QCOMPARE(stageOf(0), int(Imports::Stage::Filed));
        QCOMPARE(stageOf(1), int(Imports::Stage::Filed));
    }

    /// Out of the queue, and the server told — it keeps what nobody named, so a row that
    /// vanishes from a screen without a word leaves a file on the library's own disk.
    void abandoning_a_row_tells_the_server_to_clean_up()
    {
        m_pretend->answerFor = [](const QByteArray &) { return aReservation("rcv_9"); };
        m_imports->offer({aFile(u"Tome 4.cbz"_s)});
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
        m_imports->offer({going, aFile(u"reste.cbz"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QVERIFY(QFile::remove(going));

        m_imports->send();
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Failed)
                    || stageOf(1) == int(Imports::Stage::Failed));
        // The other one was not dragged down with it.
        QTRY_VERIFY(m_imports->inFlight() < 2);
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFile(u"A.cbz"_s), aFile(u"B.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));

        // A's cleanup is refused in the background — B does not wait on it.
        m_imports->abandon(0);
        QCOMPARE(m_imports->count(), 1);
        QCOMPARE(nameOf(0), u"B.cbz"_s);

        m_imports->send();
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

        m_imports->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

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
        QFile big(at + u"/Tome 1.cbz"_s);
        QVERIFY(big.open(QIODevice::WriteOnly));
        big.write(QByteArray(5 * 1024 * 1024, 'x'));
        big.close();
        QFile small(at + u"/Tome 2.cbz"_s);
        QVERIFY(small.open(QIODevice::WriteOnly));
        small.write(QByteArray(9, 'y'));
        small.close();

        QList<QList<int>> sentRoles;
        const auto connection = connect(
            m_imports, &QAbstractItemModel::dataChanged, m_imports,
            [&sentRoles](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.contains(int(Imports::Role::Sent)))
                    sentRoles << roles;
            });

        m_imports->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->send();
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY2(m_pretend->heard.contains("\"sidecars\""), m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("work.json"), m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.contains("Koro Quest"), m_pretend->heard.constData());
    }

    /// Creating a universe is the one thing a reader cannot undo by deleting a file, so a
    /// folder that would create something stops and waits. One that would not does not.
    void a_folder_that_would_create_something_waits_to_be_told_yes()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200,
                          R"({"id":"imp_3","root":"Koro","creates":)"
                          R"([{"kind":"WORK","name":"Koro Quest","at":""}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));

        const QVariantList made = m_imports->data(m_imports->index(0, 0),
                                                  int(Imports::Role::Creates)).toList();
        QCOMPARE(made.size(), 1);
        QCOMPARE(made.constFirst().toMap().value(u"name"_s).toString(), u"Koro Quest"_s);

        m_imports->accept(0);
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
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

        m_imports->offer({aFolder()});
        QCOMPARE(m_imports->rowCount({}), 1);
        QCOMPARE(stageOf(0), int(Imports::Stage::Asking));
        // Three volumes of nine bytes, known from finding alone — no checksum computed yet.
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Size)).toLongLong(),
                 27LL);
        // And the queue knows it is not ready to start. Pressing « Démarrer » here would
        // find nothing ready, start nothing, and close the dialog on a transfer that never
        // began.
        QCOMPARE(m_imports->reading(), 1);

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QCOMPARE(m_imports->reading(), 0);
    }

    /// The defect measured: dropping the folder that holds every series rendered one card
    /// named after it. It renders one per declared thing instead, and the shelf itself is
    /// nowhere in the list.
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
            QFile volume(at + u"/Tome 1.cbz"_s);
            QVERIFY(volume.open(QIODevice::WriteOnly));
            volume.write(QByteArray(9, 'x'));
        }

        m_imports->offer({shelf});

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
        m_imports->offer({aFolder()});

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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s), aFolder(u"Trois"_s)});

        // Only one being verified, the others waiting their turn.
        QCOMPARE(m_imports->reading(), 1);
        QTRY_COMPARE(m_imports->reading(), 0);
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
                          R"("moves":[{"workId":"w-1","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->offer({aFolder()});
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
        m_imports->send();
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
                          R"("moves":[{"workId":"w-1","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->offer({aFolder()});
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
        m_imports->send();
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
                          R"("moves":[{"workId":"w-1","name":"Elfes",)"
                          R"("from":"/srv/leaf/library/Mangas/Elfes","at":"Elfes"}],)"
                          R"("toSend":[],"alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY2(!m_pretend->heard.contains("checksum"), m_pretend->heard.constData());
    }

    void verifying_puts_a_fingerprint_on_every_file()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_8","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});

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
        for (int i = 0; i < Files; ++i) {
            QFile file(at + u"/Tome %1.cbz"_s.arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(6 * 1024 * 1024, 'x'));
        }

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

        m_imports->offer({at});
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
        m_imports->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
        QCOMPARE(m_imports->rowCount({}), 2);

        // The first queued is the first walked — `describeNext` takes them in order.
        QCOMPARE(
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString(),
            Words::checking(0, 3));
        // The second is genuinely waiting, not stuck: « en attente ».
        QCOMPARE(
            m_imports->data(m_imports->index(1), int(Imports::Role::Checking)).toString(),
            Words::waitingToBeChecked());

        QTRY_COMPARE(m_imports->reading(), 0);
    }

    /// Without a checksum the walk never reports progress — `describe()` skips the
    /// `QPromise` entirely, and never sets `m_walkingToken` either, because a stat-only
    /// pass is over before a second report could matter. `Role::Checking` reads that row
    /// the same way it reads one still queued behind another — neither is the token
    /// actually on the pool — so both say « en attente », which is also exactly what the
    /// spec asks for here: without checksums, no card passes through « Vérification » at
    /// all. Read back synchronously, the same way `checking_carries_a_count_from_the_first_
    /// frame` does for the verifying case: no progress call can have landed yet either way,
    /// which is the worst case for a stuck "0/3" to show up in.
    void not_verifying_says_waiting_never_verification()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"imp_nv","root":"Koro","creates":[],"toSend":[],)"
                               R"("alreadyThere":[],"bytesToSend":0})");
        };
        m_imports->setVerifying(false);
        m_imports->offer({aFolder()});

        const QString checking =
            m_imports->data(m_imports->index(0), int(Imports::Role::Checking)).toString();
        QCOMPARE(checking, Words::waitingToBeChecked());
        QVERIFY2(!checking.contains(u'/'), "a dead fraction, not the word for no checksum");
        QVERIFY2(checking != Words::importStage(Words::Importing::Asking),
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
        m_imports->offer({aFolder(u"Un"_s), aFolder(u"Deux"_s)});
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
        m_imports->offer({m_folder.filePath(u"absent"_s)});
        QCOMPARE(m_imports->count(), 0);
    }

    /// A malformed answer is refused and names what it could not read, rather than leaving
    /// a row that says nothing and never moves.
    void an_answer_the_client_cannot_read_fails_the_row_and_says_why()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return aReply(200, R"({"id":"rcv_11"})");
        };
        m_imports->offer({aFile(u"Tome 5.cbz"_s)});

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
            QFile volume(at + u"/Tome 1.cbz"_s);
            [&] { QVERIFY(volume.open(QIODevice::WriteOnly)); }();
            volume.write(QByteArray(9, 'x'));
        }

        m_imports->offer({shelf});

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
        m_imports->offer({aFolder()});
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

        m_imports->send();
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_imports->toggle(0, QString());

        m_imports->send();
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
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
                                   R"("declarations":[{"path":"work.json",)"
                                   R"("presentName":"Koro Quest","differs":["summary"]}],)"
                                   R"("toSend":[],"alreadyThere":["Tome 1.cbz"],)"
                                   R"("bytesToSend":0})");
            }
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_imports->offer({aFolder()});
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

        m_imports->send();
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
        m_imports->offer({aFolder()});

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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        // Clear by default: nothing is overwritten by having been dropped.
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 2.cbz"_s));

        m_imports->setReplacing(0, u"Tome 1.cbz"_s, true);
        QVERIFY(m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 1.cbz"_s));
        QVERIFY(!m_imports->ticked(0, Imports::Choice::Replacing, u"Tome 2.cbz"_s));

        m_imports->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        const QByteArray heard = m_pretend->heard;
        QVERIFY2(heard.contains("\"replace\":[\"Tome 1.cbz\"]"), heard.constData());
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Deciding));

        const QVariantList nodes =
            m_imports->data(m_imports->index(0, 0), int(Imports::Role::Nodes)).toList();
        QCOMPARE(nodes.constFirst().toMap().value(u"state"_s).toString(),
                 Words::willBeCreated(Manifest::Level::Work));
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
                                         "creates",  "moves",      "nodes",    "checking"};
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
                          R"("moves":[{"workId":"w-1","name":"Koro Quest","from":"M/Koro",)"
                          R"("at":""}]})");
        };
        m_imports->offer({aFolder()});
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
            m_imports->pause(row);
            m_imports->resume(row);
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
        m_imports->offer({aFile(u"Tome 9.cbz"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
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
        m_imports->offer({aFile(u"Tome 5.cbz"_s), aFile(u"Tome 6.cbz"_s)});
        QTRY_COMPARE(m_imports->count(), 2);
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));

        m_imports->giveUpPreparing();

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
                              R"("moves":[{"workId":"w-2","name":"Mages",)"
                              R"("from":"Mangas/Mages","at":"Mages"}],)"
                              R"("toSend":["Elfes/Tome 1.cbz"],)"
                              R"("alreadyThere":["Mages/Tome 1.cbz"],"bytesToSend":9})");
            return aReply(200, R"({"path":"Elfes/Tome 1.cbz","received":9})");
        };
        m_imports->offer({aUniverse()});
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
        m_imports->send();
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
                          R"("declarations":[{"path":"work.json","presentName":"Koro",)"
                          R"("differs":["titre"]}]})");
        };
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFile(u"Tome 20.cbz"_s)});
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
        m_imports->offer({aFolder()});
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
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
        m_imports->offer({aFolder()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        m_imports->send();
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
        m_imports->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));

        QVERIFY(QFile::remove(at + u"/Tome 1.cbz"_s));
        m_imports->send();

        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QCOMPARE(m_imports->data(m_imports->index(0, 0), int(Imports::Role::Trouble)).toString(),
                 Words::couldNotBeRead(u"Tome 1.cbz"_s));
    }
};

QTEST_MAIN(HoldsTheImports)
#include "holds_the_imports.moc"
