// The draft of a universe's reading orders, while one of them is written.
//
// What is worth covering is what the draft writes and what it refuses: an order written
// again inside another is offered as an inclusion, and saving is what accepts it; every key
// nobody touched leaves as it came, down to a step's; an inclusion that would loop, a range
// that runs backwards or one that cannot say which edition it counts are refused here, before
// the announcement would refuse them; and a step naming a work this drop does not hold is
// kept, since the work may well be on the server's disk.

#include "Carrier.h"
#include "ImportOffers.h"
#include "ImportQueue.h"
#include "Imports.h"
#include "OrderEditor.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"
#include "Zip.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

void writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void aVolume(const QString &path, int number)
{
    QFile file(path);
    [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
    file.write(Zip::volume(R"({"leaf":1,"number":)" + QByteArray::number(number) + '}'));
}

/// A series saying its title and direction, and `extra` besides, of `volumes` volumes.
void aSaidSeries(const QString &folder, const QString &title, int volumes,
                 const QJsonObject &extra = {})
{
    [&] { QVERIFY(QDir().mkpath(folder)); }();
    QJsonObject work{{u"leaf"_s, 1}, {u"title"_s, title},
                     {u"readingDirection"_s, u"LEFT_TO_RIGHT"_s},
                     {u"authors"_s, QJsonArray{u"Scénariste"_s}},
                     {u"artists"_s, QJsonArray{u"Dessinateur"_s}}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        work.insert(it.key(), it.value());
    writeJson(folder + u"/work.json"_s, work);
    for (int number = 1; number <= volumes; ++number)
        aVolume(folder + u"/Tome %1.cbz"_s.arg(number), number);
}

QJsonObject aStep(const QString &work, const QString &unit = {}, int from = 0, int to = 0)
{
    QJsonObject step{{u"work"_s, work}};
    if (!unit.isEmpty()) {
        step.insert(u"unit"_s, unit);
        step.insert(u"from"_s, from);
        step.insert(u"to"_s, to);
    }
    return step;
}

} // namespace

class EditsAnOrder : public QObject
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
    OrderEditor *m_editor = nullptr;
    int m_opened = 0;

    /// « Terres d’Arran », as read from the file: what saving must leave as it was.
    QJsonObject m_arran;

    /// A universe of six series and three orders. « Tout Aquilon » writes again, in its
    /// middle, every step of « Terres d’Arran »; « Hors-série » reads a work this drop does not
    /// hold; « Gnomes de Troy » comes in two editions. Keys no version of Leaf reads sit on the
    /// file, on an order and on a step. `more` orders follow the three.
    QString anAquilon(const QJsonArray &more = {})
    {
        const QString at = m_folder.filePath(u"Aquilon"_s);
        const QString arran = u"Terres d’Arran"_s;
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QJsonObject first = aStep(u"Lanfeust de Troy"_s, u"VOLUME"_s, 1, 4);
        first.insert(u"couleur"_s, u"vert"_s);
        const QJsonArray all{first,
                             aStep(u"Elfes"_s),
                             aStep(u"Nains"_s),
                             aStep(u"Mages"_s),
                             aStep(u"Lanfeust de Troy"_s, u"VOLUME"_s, 5, 8),
                             aStep(u"Trolls de Troy"_s, u"CHAPTER"_s, 1, 12),
                             aStep(u"Elfes"_s, u"VOLUME"_s, 5, 6)};
        m_arran = QJsonObject{{u"id"_s, u"terres-d-arran"_s},
                              {u"name"_s, arran},
                              {u"steps"_s, QJsonArray{aStep(u"Elfes"_s), aStep(u"Nains"_s),
                                                      aStep(u"Mages"_s)}}};
        QJsonArray orders{QJsonObject{{u"id"_s, u"tout-aquilon"_s},
                                      {u"name"_s, u"Tout Aquilon"_s},
                                      {u"note"_s, u"la saga"_s},
                                      {u"steps"_s, all}},
                          m_arran,
                          QJsonObject{{u"id"_s, u"hors-serie"_s},
                                      {u"name"_s, u"Hors-série"_s},
                                      {u"steps"_s, QJsonArray{aStep(
                                                       u"Les Conquérants de Troy"_s)}}}};
        for (const QJsonValue &one : more)
            orders << one;
        writeJson(at + u"/universe.json"_s,
                  {{u"leaf"_s, 1},
                   {u"name"_s, u"Monde d’Aquilon"_s},
                   {u"couleur"_s, u"bleu"_s},
                   {u"defaultOrder"_s, u"tout-aquilon"_s},
                   {u"orders"_s, orders}});
        const QJsonObject tagged{{u"tags"_s, QJsonArray{arran}}};
        aSaidSeries(at + u"/Elfes"_s, u"Elfes"_s, 6, tagged);
        aSaidSeries(at + u"/Nains"_s, u"Nains"_s, 3, tagged);
        aSaidSeries(at + u"/Mages"_s, u"Mages"_s, 3, tagged);
        aSaidSeries(at + u"/Lanfeust de Troy"_s, u"Lanfeust de Troy"_s, 8);
        aSaidSeries(at + u"/Trolls de Troy"_s, u"Trolls de Troy"_s, 2);
        aSaidSeries(at + u"/Gnomes de Troy"_s, u"Gnomes de Troy"_s, 0);
        for (const QString &edition : {u"Intégrale"_s, u"Originale"_s}) {
            [&] { QVERIFY(QDir().mkpath(at + u"/Gnomes de Troy/"_s + edition)); }();
            writeJson(at + u"/Gnomes de Troy/"_s + edition + u"/edition.json"_s,
                      {{u"leaf"_s, 1}, {u"name"_s, edition}});
            aVolume(at + u"/Gnomes de Troy/"_s + edition + u"/Tome 1.cbz"_s, 1);
        }
        return at;
    }

    /// One session per announcement, asking for one volume: a drop with nothing to send is let
    /// go the moment « Importer » is pressed, and would never be on its way.
    std::function<QByteArray(const QByteArray &)> sessions()
    {
        return [this](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (request.startsWith("POST /import ")) {
                const QByteArray id = "imp_s" + QByteArray::number(++m_opened);
                return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],)"
                                   R"("toSend":["Elfes/Tome 1.cbz"],"alreadyThere":[],"bytesToSend":1})");
            }
            return QByteArray();
        };
    }

    int stageOf(int row) const
    {
        return m_imports->data(m_imports->index(row), int(Imports::Role::Stage_)).toInt();
    }

    /// The drop of the universe, read and waiting at the checking step; the id of its sheet.
    QString aquilonRead(const QJsonArray &more = {})
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({anAquilon(more)});
        [&] {
            QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                        || stageOf(0) == int(Imports::Stage::Reviewing));
        }();
        return QString::number(m_imports->cards().at(0).token) + u'/';
    }

    /// The JSON body of every `POST /import` heard, in the order they arrived.
    QList<QJsonObject> announcements() const
    {
        QList<QJsonObject> out;
        const QByteArray &heard = m_pretend->heard;
        const QByteArray line = "POST /import ";
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

    /// The `universe.json` the last announcement carried, or nothing.
    QJsonObject announcedUniverse() const
    {
        const QList<QJsonObject> all = announcements();
        for (auto body = all.crbegin(); body != all.crend(); ++body) {
            for (const QJsonValue &one : body->value(u"sidecars"_s).toArray()) {
                if (one.toObject().value(u"path"_s).toString() == u"universe.json"_s)
                    return QJsonDocument::fromJson(
                               one.toObject().value(u"json"_s).toString().toUtf8())
                        .object();
            }
        }
        return {};
    }

    /// Confirms the drop, and the orders the announcement that follows carries: the one the
    /// drop was read with carried them too, as they were before anything was saved.
    QJsonArray confirmedOrders()
    {
        const qsizetype before = announcements().size();
        m_queue->confirm();
        [&] { QTRY_VERIFY(announcements().size() > before); }();
        return announcedUniverse().value(u"orders"_s).toArray();
    }

    QVariantMap stepAt(int at) const { return m_editor->steps().value(at).toMap(); }

    QVariantMap targetNamed(const QString &id) const
    {
        for (const QVariant &one : m_editor->step().value(u"targets"_s).toList()) {
            if (one.toMap().value(u"id"_s).toString() == id)
                return one.toMap();
        }
        return {};
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
        m_offers = new ImportOffers(m_imports, m_imports);
        m_queue = new ImportQueue(m_imports, m_imports);
        m_editor = new OrderEditor(m_imports);
    }

    void cleanup()
    {
        delete m_editor;
        delete m_imports;
        delete m_server;
        delete m_settings;
        delete m_pretend;
        QDir(m_folder.path()).removeRecursively();
        QDir().mkpath(m_folder.path());
    }

    /// An order that writes again every step of another is opened with the inclusion offered
    /// in their place, and the steps it would read shown under it: written twice, « Terres
    /// d’Arran » changed in one place and not the other, and nothing said so.
    void opening_an_order_that_copies_another_proposes_to_include_it()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        QVERIFY(m_editor->open());

        const QVariantList steps = m_editor->steps();
        QCOMPARE(steps.size(), 5);
        const QVariantMap proposed = stepAt(1);
        QCOMPARE(proposed.value(u"kind"_s).toString(), u"include"_s);
        QVERIFY(proposed.value(u"proposed"_s).toBool());
        QCOMPARE(proposed.value(u"label"_s).toString(), Words::includeStep(u"Terres d’Arran"_s));
        const QVariantList inherited = proposed.value(u"inherited"_s).toList();
        QCOMPARE(inherited.size(), 3);
        QCOMPARE(inherited.at(0).toMap().value(u"label"_s).toString(), u"Elfes"_s);
        QCOMPARE(inherited.at(0).toMap().value(u"unit"_s).toString(), Words::orderUnit({}));
        QCOMPARE(stepAt(0).value(u"label"_s).toString(), u"Lanfeust de Troy"_s);
        QCOMPARE(stepAt(0).value(u"from"_s).toString(), u"1"_s);
        QCOMPARE(stepAt(0).value(u"to"_s).toString(), u"4"_s);
        QCOMPARE(stepAt(3).value(u"unitLabel"_s).toString(), Words::orderUnit(u"CHAPTER"_s));
        QVERIFY(!stepAt(0).value(u"proposed"_s).toBool());
        QCOMPARE(m_editor->footer(), Words::orderFooter(5, 7));
        QCOMPARE(m_editor->universeName(), u"Monde d’Aquilon"_s);
        QCOMPARE(m_editor->works().size(), 6);
        QVERIFY(m_editor->savable());
        QVERIFY(m_editor->dirty());
        QCOMPARE(m_editor->saveLabel(), Words::saveOrders(1));

        const QVariantList tabs = m_editor->tabs();
        QCOMPARE(tabs.size(), 3);
        QCOMPARE(tabs.at(0).toMap().value(u"name"_s).toString(), u"Tout Aquilon"_s);
        QCOMPARE(tabs.at(0).toMap().value(u"works"_s).toInt(), 5);
        QVERIFY(tabs.at(0).toMap().value(u"chosen"_s).toBool());
        QCOMPARE(tabs.at(1).toMap().value(u"works"_s).toInt(), 3);

        // Selected from the start, being the one step nobody wrote, the inclusion says what
        // it reads and where it is changed.
        QVERIFY(stepAt(1).value(u"selected"_s).toBool());
        m_editor->select(1);
        QCOMPARE(m_editor->step().value(u"kind"_s).toString(), u"include"_s);
        QCOMPARE(m_editor->step().value(u"target"_s).toString(), u"terres-d-arran"_s);
        QCOMPARE(m_editor->step().value(u"inheritedCount"_s).toInt(), 3);
        QCOMPARE(m_editor->step().value(u"flatCount"_s).toInt(), 7);
        QCOMPARE(m_editor->step().value(u"inheritedNote"_s).toString(),
                 Words::inheritedFrom(u"Terres d’Arran"_s));
        QVERIFY(stepAt(1).value(u"selected"_s).toBool());

        // Refused, the proposal gives back the steps it stood for.
        m_editor->remove(1);
        QCOMPARE(m_editor->steps().size(), 7);
        QCOMPARE(stepAt(1).value(u"label"_s).toString(), u"Elfes"_s);
        QCOMPARE(m_editor->footer(), Words::orderFooter(7, 7));
    }

    /// Saving writes the inclusion, and every key no one touched leaves as it came: the other
    /// orders as they were read, the keys of the file, of an order and of a step that no
    /// version of Leaf reads.
    void saving_writes_the_include_and_keeps_every_other_key()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        QVERIFY(m_editor->save());
        QVERIFY(!m_editor->open());
        QVERIFY(m_imports->cards().at(0).road.edited);

        const QJsonArray orders = confirmedOrders();
        QCOMPARE(orders.size(), 3);
        const QJsonObject tout = orders.at(0).toObject();
        const QJsonArray steps = tout.value(u"steps"_s).toArray();
        QCOMPARE(steps.size(), 5);
        QCOMPARE(steps.at(1).toObject(), (QJsonObject{{u"include"_s, u"terres-d-arran"_s}}));
        QCOMPARE(steps.at(0).toObject().value(u"couleur"_s).toString(), u"vert"_s);
        QCOMPARE(steps.at(0).toObject().value(u"from"_s).toDouble(), 1.0);
        QCOMPARE(steps.at(3).toObject().value(u"unit"_s).toString(), u"CHAPTER"_s);
        QCOMPARE(tout.value(u"note"_s).toString(), u"la saga"_s);
        QCOMPARE(orders.at(1).toObject(), m_arran);
        QCOMPARE(announcedUniverse().value(u"couleur"_s).toString(), u"bleu"_s);
        QCOMPARE(announcedUniverse().value(u"defaultOrder"_s).toString(), u"tout-aquilon"_s);
    }

    /// « Retour » leaves the draft where it was: nothing is corrected, nothing re-announced.
    void going_back_writes_nothing()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->remove(0);
        m_editor->addWork(u"Elfes"_s);
        m_editor->close();

        QVERIFY(!m_editor->open());
        QVERIFY(m_editor->steps().isEmpty());
        QVERIFY(!m_imports->cards().at(0).road.edited);
        // Opened again, it is the file's, and not the draft let go.
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        QCOMPARE(stepAt(0).value(u"label"_s).toString(), u"Lanfeust de Troy"_s);
        QCOMPARE(m_editor->steps().size(), 5);
    }

    /// Within « Terres d’Arran », which « Tout Aquilon » now includes, including « Tout
    /// Aquilon » would bring it back to itself: the choice is greyed with its reason, and
    /// insisting changes nothing. The order itself is greyed too.
    void an_order_that_would_loop_is_greyed_with_its_reason()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->choose(u"terres-d-arran"_s);
        QVERIFY(m_editor->tabs().at(1).toMap().value(u"chosen"_s).toBool());

        const QVariantMap tout = targetNamed(u"tout-aquilon"_s);
        QVERIFY(!tout.isEmpty());
        QVERIFY(!tout.value(u"possible"_s).toBool());
        QCOMPARE(tout.value(u"reason"_s).toString(),
                 Words::includeWouldLoop(u"Terres d’Arran"_s, u"Tout Aquilon"_s));
        QVERIFY(!targetNamed(u"terres-d-arran"_s).value(u"possible"_s).toBool());
        QVERIFY(targetNamed(u"hors-serie"_s).value(u"possible"_s).toBool());
        // The same, whatever step is selected: what « + Inclure un ordre » offers.
        QCOMPARE(m_editor->targets(), m_editor->step().value(u"targets"_s).toList());

        QVERIFY(!m_editor->addInclude(u"tout-aquilon"_s));
        QCOMPARE(m_editor->steps().size(), 3);
        QVERIFY(!m_editor->setInclude(0, u"tout-aquilon"_s));
        QCOMPARE(stepAt(0).value(u"kind"_s).toString(), u"work"_s);
        // Back on « Tout Aquilon », the proposal is still there: the tabs keep the draft.
        m_editor->choose(u"tout-aquilon"_s);
        QVERIFY(stepAt(1).value(u"proposed"_s).toBool());
        QVERIFY(m_editor->addInclude(u"hors-serie"_s));
        QCOMPARE(m_editor->steps().size(), 6);
    }

    /// A range from 4 to 1 is refused and said on its step, which keeps what it held: the
    /// scan leaves such a step out, and the reader would have found it gone.
    void a_range_running_backwards_is_refused_and_said()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));

        QVERIFY(!m_editor->setRange(0, u"VOLUME"_s, {}, u"4"_s, u"1"_s));
        QCOMPARE(stepAt(0).value(u"doubt"_s).toString(), Words::rangeBackwards());
        QCOMPARE(stepAt(0).value(u"from"_s).toString(), u"1"_s);
        QCOMPARE(stepAt(0).value(u"to"_s).toString(), u"4"_s);

        QVERIFY(m_editor->setRange(0, u"VOLUME"_s, {}, u"2"_s, u"4"_s));
        QCOMPARE(stepAt(0).value(u"doubt"_s).toString(), QString());
        QCOMPARE(stepAt(0).value(u"from"_s).toString(), u"2"_s);
        // A word where a number goes is no number either.
        QVERIFY(!m_editor->setRange(0, u"VOLUME"_s, {}, u"deux"_s, u"4"_s));
        QCOMPARE(stepAt(0).value(u"doubt"_s).toString(), Words::notANumber(u"deux"_s, false));
        // A range of chapters names no edition, as the scan reads one.
        QVERIFY(!m_editor->setRange(2, u"CHAPTER"_s, u"Intégrale"_s, u"1"_s, u"3"_s));
        QCOMPARE(stepAt(2).value(u"doubt"_s).toString(), Words::chapterRangeNamesNoEdition());
    }

    /// A range of volumes on a work of two editions has to say which, since « tome 3 » is not
    /// the same book in both: the step asks, offers the two, and the order is not saved
    /// until it is answered.
    void a_volume_range_on_a_work_of_two_editions_asks_which()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->addWork(u"Gnomes de Troy"_s);
        QCOMPARE(m_editor->steps().size(), 6);
        QVERIFY(stepAt(5).value(u"selected"_s).toBool());

        QVERIFY(m_editor->setRange(5, u"VOLUME"_s, {}, u"1"_s, u"2"_s));
        QCOMPARE(stepAt(5).value(u"doubt"_s).toString(),
                 Words::rangeNeedsEdition(u"Gnomes de Troy"_s));
        QCOMPARE(m_editor->step().value(u"editions"_s).toStringList(),
                 (QStringList{u"Intégrale"_s, u"Originale"_s}));
        QVERIFY(!m_editor->savable());
        QVERIFY(!m_editor->save());
        QVERIFY(m_editor->open());

        QVERIFY(m_editor->setRange(5, u"VOLUME"_s, u"Intégrale"_s, u"1"_s, u"2"_s));
        QCOMPARE(stepAt(5).value(u"doubt"_s).toString(), QString());
        QCOMPARE(stepAt(5).value(u"edition"_s).toString(), u"Intégrale"_s);
        // A work of one edition asks nothing: there is none to choose.
        QCOMPARE(stepAt(0).value(u"doubt"_s).toString(), QString());
        QVERIFY(m_editor->save());
    }

    /// A step naming a work this drop does not hold is kept and said absent: the work may be
    /// on the server already, and a step dropped because it was not in sight is a reading
    /// order cut behind its author's back.
    void a_step_of_a_work_not_in_the_drop_is_kept_and_said_absent()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"hors-serie"_s));
        QCOMPARE(stepAt(0).value(u"label"_s).toString(), u"Les Conquérants de Troy"_s);
        QVERIFY(stepAt(0).value(u"absent"_s).toBool());
        QVERIFY(!m_editor->step().value(u"works"_s).toStringList().contains(
            u"Les Conquérants de Troy"_s));
        QVERIFY(m_editor->step().value(u"works"_s).toStringList().contains(u"Elfes"_s));

        m_editor->addWork(u"Elfes"_s);
        QVERIFY(!stepAt(1).value(u"absent"_s).toBool());
        QVERIFY(m_editor->save());
        const QJsonArray orders = confirmedOrders();
        QCOMPARE(orders.size(), 3);
        const QJsonArray steps = orders.at(2).toObject().value(u"steps"_s).toArray();
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps.at(0).toObject(), aStep(u"Les Conquérants de Troy"_s));
        QCOMPARE(steps.at(1).toObject(), aStep(u"Elfes"_s));
    }

    /// A new order gets an id of its own, its name in lowercase words, and is written with
    /// it: an order without one is designated by its name, and renamed, every inclusion of
    /// it would name nothing.
    void a_new_order_gets_its_own_id_and_is_written()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        const QString id = m_editor->newOrder(u"Les Gnomes"_s);
        QCOMPARE(id, u"les-gnomes"_s);
        QCOMPARE(m_editor->tabs().size(), 4);
        QVERIFY(m_editor->tabs().at(3).toMap().value(u"chosen"_s).toBool());
        QVERIFY(m_editor->steps().isEmpty());
        QCOMPARE(m_editor->footer(), Words::orderFooter(0, 0));
        m_editor->addWork(u"Gnomes de Troy"_s);
        QCOMPARE(m_editor->saveLabel(), Words::saveOrders(2));
        // A second one of that name is another order, and another id.
        QCOMPARE(m_editor->newOrder(u"Les Gnomes"_s), u"les-gnomes-2"_s);
        m_editor->choose(u"les-gnomes-2"_s);
        m_editor->addWork(u"Nains"_s);

        QVERIFY(m_editor->save());
        const QJsonArray orders = confirmedOrders();
        QCOMPARE(orders.size(), 5);
        QCOMPARE(orders.at(3).toObject(),
                 (QJsonObject{{u"id"_s, u"les-gnomes"_s},
                              {u"name"_s, u"Les Gnomes"_s},
                              {u"steps"_s, QJsonArray{aStep(u"Gnomes de Troy"_s)}}}));
        QCOMPARE(orders.at(4).toObject().value(u"id"_s).toString(), u"les-gnomes-2"_s);
    }

    /// A step moved keeps every key it carried, the ones no version of Leaf reads among them:
    /// recomposed from what the editor knows, « couleur » would have been lost on the way.
    void steps_move_without_losing_their_unknown_keys()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->move(0, 4);
        QCOMPARE(stepAt(4).value(u"label"_s).toString(), u"Lanfeust de Troy"_s);
        QVERIFY(stepAt(4).value(u"selected"_s).toBool());
        QCOMPARE(stepAt(0).value(u"kind"_s).toString(), u"include"_s);
        m_editor->move(4, 3);
        m_editor->move(3, 4);

        QVERIFY(m_editor->save());
        const QJsonArray orders = confirmedOrders();
        QCOMPARE(orders.size(), 3);
        const QJsonArray steps = orders.at(0).toObject().value(u"steps"_s).toArray();
        QCOMPARE(steps.size(), 5);
        QCOMPARE(steps.at(0).toObject(), (QJsonObject{{u"include"_s, u"terres-d-arran"_s}}));
        QJsonObject moved = aStep(u"Lanfeust de Troy"_s, u"VOLUME"_s, 1, 4);
        moved.insert(u"couleur"_s, u"vert"_s);
        QCOMPARE(steps.at(4).toObject(), moved);
    }

    /// An order may hold 4 096 steps once its inclusions are read in place, and the server
    /// keeps one past it without any: the inclusion that would take it there is undone and
    /// said, and the order is not left for the scan to empty without a word.
    void a_change_past_what_the_server_reads_is_undone_and_said()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->newOrder(u"Tout, encore"_s);
        // Seven steps each: 585 of them are 4 095 once flattened, and the next is one too many.
        int added = 0;
        while (added < 1000 && m_editor->addInclude(u"tout-aquilon"_s))
            ++added;
        QCOMPARE(added, 585);
        QCOMPARE(m_editor->steps().size(), 585);
        QCOMPARE(stepAt(584).value(u"doubt"_s).toString(), Words::orderPastTheCeiling());
        QCOMPARE(m_editor->footer(), Words::orderFooter(585, 4095));
        // What was refused is not in the draft: it saves.
        QVERIFY(m_editor->save());
    }

    /// Once « Importer » has let the card go, its orders are no longer opened: a draft saved
    /// then would correct a card whose announcement is already made.
    void an_order_cannot_be_opened_once_its_card_is_on_its_way()
    {
        const QString universe = aquilonRead();
        m_queue->confirm();
        QTRY_VERIFY(!Carrier::correctable(m_imports->cards().at(0)));

        QVERIFY(!m_editor->openOn(universe, u"tout-aquilon"_s));
        QVERIFY(!m_editor->open());
        QVERIFY(m_editor->tabs().isEmpty());
        // Nor is anything opened on what is no universe.
        QVERIFY(!m_editor->openOn(universe + u"Elfes"_s, u"tout-aquilon"_s));
    }

    /// An inclusion writes what the server resolves to the order chosen, and nothing else.
    /// By its name, « Les Nains » went to the order that wrote « cycle » as its id and that
    /// name before it; and an order with neither id nor name was written as an empty step.
    void an_inclusion_writes_what_designates_its_order_and_nothing_else()
    {
        const QJsonArray more{
            QJsonObject{{u"id"_s, u"cycle"_s},
                        {u"name"_s, u"Les Nains"_s},
                        {u"steps"_s, QJsonArray{aStep(u"Gnomes de Troy"_s)}}},
            QJsonObject{{u"name"_s, u"Les Nains"_s},
                        {u"steps"_s, QJsonArray{aStep(u"Trolls de Troy"_s)}}},
            QJsonObject{{u"steps"_s, QJsonArray{aStep(u"Nains"_s, u"VOLUME"_s, 1, 2)}}}};
        const QString universe = aquilonRead(more);
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        QCOMPARE(m_editor->steps().size(), 5);

        QVERIFY(m_editor->addInclude(u"les nains"_s));
        QVERIFY(m_editor->addInclude(u"6"_s));
        QCOMPARE(stepAt(5).value(u"doubt"_s).toString(), QString());
        QCOMPARE(stepAt(6).value(u"doubt"_s).toString(), QString());
        QCOMPARE(stepAt(6).value(u"label"_s).toString(), Words::includeStep(u"6"_s));

        QVERIFY(m_editor->save());
        const QJsonArray steps =
            confirmedOrders().at(0).toObject().value(u"steps"_s).toArray();
        QCOMPARE(steps.size(), 7);
        QCOMPARE(steps.at(1).toObject(), (QJsonObject{{u"include"_s, u"terres-d-arran"_s}}));
        QCOMPARE(steps.at(5).toObject(), (QJsonObject{{u"include"_s, u"les nains"_s}}));
        QCOMPARE(steps.at(6).toObject(), (QJsonObject{{u"include"_s, u"6"_s}}));
    }

    /// An order bearing the id of one written before it is disregarded by the scan: offered
    /// greyed, since no inclusion can reach it, and costing the universe nothing. Four of them
    /// under the id of « Hors-série » once took the universe past what it may hold.
    void an_order_disregarded_for_its_id_is_offered_greyed_and_costs_nothing()
    {
        QJsonArray many;
        for (int at = 0; at < Orders::MostFlatSteps; ++at)
            many << aStep(u"Elfes"_s);
        QJsonArray more;
        for (int copy = 0; copy < 4; ++copy)
            more << QJsonObject{{u"id"_s, u"hors-serie"_s},
                                {u"name"_s, u"Doublon"_s},
                                {u"steps"_s, many}};
        const QString universe = aquilonRead(more);
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));

        const QVariantList targets = m_editor->targets();
        QCOMPARE(targets.size(), 7);
        QVERIFY(targets.at(2).toMap().value(u"possible"_s).toBool());
        for (int at = 3; at < 7; ++at) {
            QVERIFY(!targets.at(at).toMap().value(u"possible"_s).toBool());
            QCOMPARE(targets.at(at).toMap().value(u"reason"_s).toString(),
                     Words::orderDisregarded(u"Doublon"_s));
        }
        m_editor->addWork(u"Nains"_s);
        QCOMPARE(m_editor->steps().size(), 6);
        QCOMPARE(stepAt(5).value(u"doubt"_s).toString(), QString());
        QVERIFY(m_editor->savable());
    }

    /// An `include` naming nothing is an inclusion left to finish, and is finished on its
    /// sheet: read as a step of a work, it offered no order to choose, and an inclusion that
    /// also wrote a work kept its refusal after the work had gone.
    void an_inclusion_naming_nothing_is_finished_and_holds_the_save_no_longer()
    {
        const QJsonArray more{QJsonObject{
            {u"id"_s, u"a-finir"_s},
            {u"name"_s, u"À finir"_s},
            {u"steps"_s, QJsonArray{QJsonObject{{u"include"_s, u""_s}},
                                    QJsonObject{{u"include"_s, u"hors-serie"_s},
                                                {u"work"_s, u"Elfes"_s}}}}}};
        const QString universe = aquilonRead(more);
        QVERIFY(m_editor->openOn(universe, u"a-finir"_s));
        QCOMPARE(stepAt(0).value(u"kind"_s).toString(), u"include"_s);
        QCOMPARE(stepAt(1).value(u"kind"_s).toString(), u"include"_s);
        QVERIFY(!m_editor->savable());

        QVERIFY(m_editor->setInclude(0, u"hors-serie"_s));
        QVERIFY(m_editor->setInclude(1, u"terres-d-arran"_s));
        QCOMPARE(stepAt(0).value(u"doubt"_s).toString(), QString());
        QCOMPARE(stepAt(1).value(u"doubt"_s).toString(), QString());
        QVERIFY(m_editor->savable());
        QVERIFY(m_editor->save());
        const QJsonArray steps =
            confirmedOrders().at(3).toObject().value(u"steps"_s).toArray();
        QCOMPARE(steps, (QJsonArray{QJsonObject{{u"include"_s, u"hors-serie"_s}},
                                    QJsonObject{{u"include"_s, u"terres-d-arran"_s}}}));
    }

    /// The draft is worked out once per change, whatever reads it — the tabs, the steps, the
    /// step chosen, the footer and the button each asked for it again, and each read every
    /// order of the universe down to the last inclusion.
    void the_draft_is_worked_out_once_per_change_whatever_reads_it()
    {
        const QString universe = aquilonRead();
        QVERIFY(m_editor->openOn(universe, u"tout-aquilon"_s));
        m_editor->select(1);

        const OrderDraft::Cost before = OrderDraft::cost();
        for (int again = 0; again < 3; ++again) {
            (void)m_editor->tabs();
            (void)m_editor->steps();
            (void)m_editor->step();
            (void)m_editor->footer();
            (void)m_editor->saveLabel();
            (void)m_editor->dirty();
        }
        QCOMPARE(OrderDraft::cost().drafts - before.drafts, 1);
        // Bounded by what the server lets a universe look at, and not by what is asked.
        QVERIFY(OrderDraft::cost().looks - before.looks <= Orders::MostUniverseLooks + 64);
    }
};

QTEST_MAIN(EditsAnOrder)
#include "edits_an_order.moc"
