// The checking frame, as the model a screen binds to: a tree, a list, four counters, a
// sheet and a footer.
//
// Tested here and not through a window because every one of these is a decision — what
// opens first, what counts as identical, what the footer counts — and a decision drawn is a
// decision nobody can assert on. The cards are real: a folder on disk, walked, announced to a
// forty-line server that answers what the library already holds.

#include "Card.h"
#include "Carrier.h"
#include "ImportOffers.h"
#include "ImportQueue.h"
#include "ImportTally.h"
#include "Imports.h"
#include "Pretend.h"
#include "Review.h"
#include "Sagas.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"
#include "Zip.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace Qt::StringLiterals;

namespace {

QByteArray aReply(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QByteArray listed(const QStringList &names)
{
    QByteArray out = "[";
    for (const QString &name : names)
        out += (out.size() > 1 ? ",\"" : "\"") + name.toUtf8() + '"';
    return out + ']';
}

/// The server's answer to one announced folder: what it wants and what it already holds.
QByteArray anOpening(const QByteArray &root, const QStringList &toSend,
                     const QStringList &alreadyThere, const QByteArray &creates = "[]")
{
    return aReply(200, "{\"id\":\"imp_" + root.left(4) + "\",\"root\":\"" + root
                           + "\",\"creates\":" + creates + ",\"toSend\":" + listed(toSend)
                           + ",\"alreadyThere\":" + listed(alreadyThere)
                           + ",\"bytesToSend\":9}");
}

/// The server's answer to a file dropped alone: one series, certain — the card is ready.
QByteArray aReservation(const QByteArray &id)
{
    return aReply(200, "{\"id\":\"" + id + "\",\"proposal\":{\"received\":\"" + id
                           + "\",\"name\":\"Tome.cbz\",\"size\":9,\"read\":{},"
                             "\"confidence\":\"CERTAIN\",\"reason\":\"une seule série\","
                             "\"candidates\":[{\"seriesId\":\"ed-1\",\"name\":\"Death Note\"}]}}");
}

} // namespace

class HoldsTheReview : public QObject
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
    Review *m_review = nullptr;
    int m_opened = 0;

    static QString komgaName(int number)
    {
        return u"blue_period_%1.cbz"_s.arg(number, 2, 10, QChar(u'0'));
    }

    /// A series the way a Komga library leaves one: no `work.json`, and in every archive a
    /// ComicInfo.xml that says the series, its author, its publisher, its genres and the
    /// volume's number — except volume `unnumbered`, whose ComicInfo says no number, so that
    /// its number can only be guessed from its name.
    QString aKomgaSeries(int volumes = 14, int unnumbered = 0)
    {
        const QString at = m_folder.filePath(u"Blue Period"_s);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        for (int number = 1; number <= volumes; ++number) {
            const QByteArray said = number == unnumbered
                ? QByteArray()
                : "<Number>" + QByteArray::number(number) + "</Number>";
            QFile file(at + u'/' + komgaName(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}, "<ComicInfo><Series>Blue Period</Series>" + said
                                           + "<Writer>Tsubasa Yamaguchi</Writer>"
                                             "<Penciller>Tsubasa Yamaguchi</Penciller>"
                                             "<Publisher>Pika</Publisher>"
                                             "<Genre>Seinen, Slice of life</Genre>"
                                             "</ComicInfo>"));
        }
        return at;
    }

    /// The names of volumes `from` to `to` of that series.
    static QStringList komgaNames(int from, int to)
    {
        QStringList out;
        for (int number = from; number <= to; ++number)
            out << komgaName(number);
        return out;
    }

    /// A volume whose `entry.json` says its number: nothing about it is guessed.
    static void aVolume(const QString &path, int number)
    {
        QFile file(path);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(Zip::volume(R"({"leaf":1,"number":)" + QByteArray::number(number) + '}'));
    }

    /// A `work.json` saying the title, the reading direction and who wrote and drew it: what
    /// stops the step when it is missing.
    static void aDeclaration(const QString &folder, const QByteArray &title)
    {
        QFile declaration(folder + u"/work.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(R"({"leaf":1,"title":")" + title
                          + R"(","readingDirection":"RIGHT_TO_LEFT",)"
                            R"("authors":["Toriyama"],"artists":["Toriyama"]})");
    }

    /// A series every value of which is said: it never stops to be checked.
    QString aCompleteSeries(const QString &name = u"Koro"_s)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        aDeclaration(at, "Koro Quest");
        for (int number = 1; number <= 3; ++number)
            aVolume(at + u"/Tome %1.cbz"_s.arg(number), number);
        return at;
    }

    /// A series whose `work.json` says its title and what `said` adds — a run of
    /// `,"key":value` pairs — with one volume that says its number and nothing else.
    QString aSeriesSaying(const QString &name, const QByteArray &said)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QFile declaration(at + u"/work.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(QByteArray("{\"leaf\":1,\"title\":\"") + name.toUtf8() + "\"" + said + "}");
        declaration.close();
        aVolume(at + u"/Tome 1.cbz"_s, 1);
        return at;
    }

    /// A universe that declares itself, holding two series that declare themselves too.
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

    /// The Komga series announced to a server that already holds its first `held` volumes.
    void aKomgaSeriesAnnounced(int held = 12, int unnumbered = 0)
    {
        m_pretend->answerFor = [held](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Blue Period", komgaNames(held + 1, 14), komgaNames(1, held));
            return QByteArray();
        };
        m_offers->offer({aKomgaSeries(14, unnumbered)});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Reviewing)
                    || stageOf(0) == int(Imports::Stage::Ready));
    }

    /// Says what a folder is held back for and nobody wrote — the reading direction, who wrote
    /// and drew a series (`Facts::lacks`) — as a reader types it, and waits for the card to be
    /// announced again. A shelf of unsaid folders says none of the three.
    void sayWhatIsLacking(int row)
    {
        const quint64 token = m_imports->cards().at(row).token;
        const QList<Facts::Sheet> sheets = m_imports->cards().at(row).road.sheets;
        bool said = false;
        for (const Facts::Sheet &sheet : sheets) {
            for (const Facts::Field &field : sheet.fields) {
                if (!Facts::lacks(sheet, field))
                    continue;
                if (field.key == u"readingDirection"_s) {
                    said = m_imports->edit(token, sheet.at, field.key, u"RIGHT_TO_LEFT"_s) || said;
                } else if (field.key == u"authors"_s || field.key == u"artists"_s) {
                    said = m_imports->edit(token, sheet.at, field.key,
                                           QJsonArray{u"Yuki"_s}) || said;
                }
            }
        }
        if (said)
            QTRY_VERIFY(stageOf(row) != int(Imports::Stage::Asking));
    }

    int stageOf(int row) const
    {
        return m_imports->data(m_imports->index(row), int(Imports::Role::Stage_)).toInt();
    }

    QString idOf(int row, const QString &at) const
    {
        return QString::number(m_imports->cards().at(row).token) + u'/' + at;
    }

    QVariantMap fieldOnSheet(const QString &key) const
    {
        for (const QVariant &one : m_review->sheet().value(u"fields"_s).toList()) {
            if (one.toMap().value(u"key"_s).toString() == key)
                return one.toMap();
        }
        return {};
    }

    QVariantMap countOf(Review::Filter which) const
    {
        return m_review->counts().at(int(which)).toMap();
    }

    /// The rows of the list that are lines, not group headers.
    QList<QVariantMap> lines() const
    {
        QList<QVariantMap> out;
        for (const QVariant &one : m_review->rows()) {
            if (!one.toMap().value(u"header"_s).toBool())
                out << one.toMap();
        }
        return out;
    }

    QStringList headers() const
    {
        QStringList out;
        for (const QVariant &one : m_review->rows()) {
            if (one.toMap().value(u"header"_s).toBool())
                out << one.toMap().value(u"text"_s).toString();
        }
        return out;
    }

    QVariantMap identicalHeader() const
    {
        for (const QVariant &one : m_review->rows()) {
            const QVariantMap row = one.toMap();
            if (row.value(u"header"_s).toBool()
                && row.value(u"group"_s).toInt() == int(Review::Filter::Identical))
                return row;
        }
        return {};
    }

    QStringList railIds() const
    {
        QStringList out;
        for (const QVariant &one : m_review->rail())
            out << one.toMap().value(u"id"_s).toString();
        return out;
    }

    /// Volumes `from` to `to` in `folder`, archives that say nothing about themselves: every
    /// role around them is read from the shape of the folders.
    static void bareVolumes(const QString &folder, int from, int to)
    {
        [&] { QVERIFY(QDir().mkpath(folder)); }();
        for (int number = from; number <= to; ++number) {
            QFile file(folder + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}));
        }
    }

    /// A shelf nothing in which says anything: a universe of two series, one series in two
    /// editions of `perEdition` volumes each, and a folder of scans with no title — the
    /// three things a shape is read as, side by side.
    QString aShelfOfUnsaidFolders(int perEdition = 3)
    {
        const QString bd = m_folder.filePath(u"BD"_s);
        bareVolumes(bd + u"/Terres d'Arran/Elfes"_s, 1, 4);
        bareVolumes(bd + u"/Terres d'Arran/Mages"_s, 1, 3);
        bareVolumes(bd + u"/Dragon Ball/Perfect"_s, 1, perEdition);
        bareVolumes(bd + u"/Dragon Ball/Original"_s, 1, perEdition);
        bareVolumes(bd + u"/Vrac"_s, 1, 2);
        return bd;
    }

    /// A universe that declares itself and a series in it that does too, with at its root
    /// an archive that says nothing — `scanOneShot` is the `oneshot.json` it carries, if
    /// any — and, when `withBook`, a whole book that declares itself one.
    QString aUniverseWithArchivesAtItsRoot(bool withBook, const QByteArray &scanOneShot = {})
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
        scan.write(Zip::volume({}, {}, scanOneShot));
        if (withBook) {
            QFile book(at + u"/le_combat.cbz"_s);
            [&] { QVERIFY(book.open(QIODevice::WriteOnly)); }();
            book.write(Zip::volume({}, {},
                                   R"({"leaf":1,"title":"Le Combat","readingDirection":)"
                                   R"("LEFT_TO_RIGHT"})"));
        }
        return at;
    }

    /// One session per announcement, holding nothing: every file of every card travels.
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

    int rowNamed(const QString &name) const
    {
        for (int row = 0; row < m_imports->cards().size(); ++row) {
            if (QFileInfo(m_imports->cards().at(row).path).fileName() == name)
                return row;
        }
        return -1;
    }

    QString idNamed(const QString &card, const QString &at = {}) const
    {
        return idOf(rowNamed(card), at);
    }

    /// The node of the rail named `name`, as drawn.
    QVariantMap railNamed(const QString &name) const
    {
        for (const QVariant &one : m_review->rail()) {
            if (one.toMap().value(u"name"_s).toString() == name)
                return one.toMap();
        }
        return {};
    }

    QVariantMap lineNamed(const QString &name) const
    {
        for (const QVariantMap &one : lines()) {
            if (one.value(u"name"_s).toString() == name)
                return one;
        }
        return {};
    }

    QList<QVariantMap> listOn(const QString &key) const
    {
        QList<QVariantMap> out;
        for (const QVariant &one : m_review->sheet().value(key).toList())
            out << one.toMap();
        return out;
    }

    bool sheetSays(const QString &text, bool alarming) const
    {
        return std::ranges::any_of(listOn(u"notes"_s), [&](const QVariantMap &one) {
            return one.value(u"text"_s).toString() == text
                   && one.value(u"alarming"_s).toBool() == alarming;
        });
    }

    Facts::Field fieldOnCard(int row, const QString &at, const QString &key) const
    {
        for (const Facts::Sheet &sheet : m_imports->cards().at(row).road.sheets) {
            if (sheet.at != at)
                continue;
            for (const Facts::Field &field : sheet.fields) {
                if (field.key == key)
                    return field;
            }
        }
        return {};
    }

    static void writeJson(const QString &path, const QJsonObject &object)
    {
        QFile file(path);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    /// A series in `folder` saying its title, direction, writer and drawer, and `extra`
    /// besides, holding
    /// `volumes` volumes whose `entry.json` says their number.
    /// `credited` false leaves the writer and the drawer out of its `work.json`, for a series
    /// whose volumes are the ones that name them.
    static void aSaidSeries(const QString &folder, const QString &title, int volumes,
                            const QJsonObject &extra = {}, bool credited = true)
    {
        [&] { QVERIFY(QDir().mkpath(folder)); }();
        QJsonObject work{{u"leaf"_s, 1}, {u"title"_s, title},
                         {u"readingDirection"_s, u"LEFT_TO_RIGHT"_s}};
        if (credited) {
            work.insert(u"authors"_s, QJsonArray{u"Toriyama"_s});
            work.insert(u"artists"_s, QJsonArray{u"Toriyama"_s});
        }
        for (auto it = extra.begin(); it != extra.end(); ++it)
            work.insert(it.key(), it.value());
        writeJson(folder + u"/work.json"_s, work);
        for (int number = 1; number <= volumes; ++number)
            aVolume(folder + u"/Tome %1.cbz"_s.arg(number), number);
    }

    static QJsonObject aStep(const QString &work, int from = 0, int to = 0)
    {
        QJsonObject step{{u"work"_s, work}};
        if (from > 0) {
            step.insert(u"unit"_s, u"VOLUME"_s);
            step.insert(u"from"_s, from);
            step.insert(u"to"_s, to);
        }
        return step;
    }

    /// A universe of five series, three of them tagged « Terres d’Arran », and two reading
    /// orders: « Tout Aquilon », the default, which writes again in its middle every step of
    /// « Terres d’Arran ».
    QString anAquilon()
    {
        const QString at = m_folder.filePath(u"Aquilon"_s);
        const QString arran = u"Terres d’Arran"_s;
        [&] { QVERIFY(QDir().mkpath(at)); }();
        const QJsonArray all{aStep(u"Lanfeust de Troy"_s, 1, 4), aStep(u"Elfes"_s),
                             aStep(u"Nains"_s),          aStep(u"Mages"_s),
                             aStep(u"Lanfeust de Troy"_s, 5, 8), aStep(u"Trolls de Troy"_s),
                             aStep(u"Elfes"_s, 5, 6)};
        const QJsonArray three{aStep(u"Elfes"_s), aStep(u"Nains"_s), aStep(u"Mages"_s)};
        writeJson(at + u"/universe.json"_s,
                  {{u"leaf"_s, 1},
                   {u"name"_s, u"Monde d’Aquilon"_s},
                   {u"defaultOrder"_s, u"tout-aquilon"_s},
                   {u"orders"_s,
                    QJsonArray{QJsonObject{{u"id"_s, u"tout-aquilon"_s},
                                           {u"name"_s, u"Tout Aquilon"_s},
                                           {u"steps"_s, all}},
                               QJsonObject{{u"id"_s, u"terres-d-arran"_s},
                                           {u"name"_s, arran},
                                           {u"steps"_s, three}}}}});
        const QJsonObject tagged{{u"tags"_s, QJsonArray{arran}}};
        aSaidSeries(at + u"/Elfes"_s, u"Elfes"_s, 4, tagged);
        aSaidSeries(at + u"/Nains"_s, u"Nains"_s, 3, tagged);
        aSaidSeries(at + u"/Mages"_s, u"Mages"_s, 3, tagged);
        aSaidSeries(at + u"/Lanfeust de Troy"_s, u"Lanfeust de Troy"_s, 8);
        aSaidSeries(at + u"/Trolls de Troy"_s, u"Trolls de Troy"_s, 5);
        return at;
    }

    /// A series of nine volumes each credited in its own `entry.json`, twelve people in all,
    /// volume 3 credited with nobody for the drawing — and `work` besides in its `work.json`.
    QString aCreditedSeries(const QJsonObject &work = {})
    {
        const QString at = m_folder.filePath(u"Elfes"_s);
        aSaidSeries(at, u"Elfes"_s, 0, work, false);
        const QList<std::pair<QString, QString>> credits{
            {u"A"_s, u"B"_s}, {u"C"_s, u"D"_s}, {u"E"_s, QString()}, {u"A"_s, u"F"_s},
            {u"G"_s, u"H"_s}, {u"I"_s, u"J"_s}, {u"K"_s, u"L"_s},    {u"C"_s, u"B"_s},
            {u"A"_s, u"B"_s}};
        for (int number = 1; number <= int(credits.size()); ++number) {
            const auto &[writer, drawer] = credits.at(number - 1);
            QJsonObject entry{{u"leaf"_s, 1},
                              {u"number"_s, number},
                              {u"authors"_s, QJsonArray{u"Auteur "_s + writer}}};
            if (!drawer.isEmpty())
                entry.insert(u"artists"_s, QJsonArray{u"Auteur "_s + drawer});
            QFile file(at + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
        }
        return at;
    }

    /// What the card of `row` announces for the sidecar at `path`.
    QJsonObject announced(int row, const QString &path) const
    {
        const Card &card = m_imports->cards().at(row);
        for (const Manifest::Sidecar &one :
             Facts::compose(card.road.sheets, card.road.tree.sidecars).sidecars) {
            if (one.path == path)
                return QJsonDocument::fromJson(one.json).object();
        }
        return {};
    }

    void offeredAndRead(const QStringList &folders)
    {
        m_pretend->answerFor = sessions();
        m_offers->offer(folders);
        for (int row = 0; row < folders.size(); ++row)
            QTRY_VERIFY(stageOf(row) == int(Imports::Stage::Ready)
                        || stageOf(row) == int(Imports::Stage::Reviewing));
    }

    /// Dragon Ball's three first volumes, six chapters each, under two arcs: the saga of the
    /// balls from chapter 1 to 14, the one of Piccolo from chapter 9 on.
    void aDragonBallOfThreeVolumes()
    {
        const QString series = m_folder.filePath(u"Db"_s);
        QVERIFY(QDir().mkpath(series));
        QFile sidecar(series + u"/work.json"_s);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(R"({"leaf":1,"title":"Db","readingDirection":"RIGHT_TO_LEFT",)"
                      R"("authors":["Toriyama"],"artists":["Toriyama"],)"
                      R"("arcs":[{"name":"Saga des Dragon Balls","unit":"CHAPTER","from":1,"to":14},)"
                      R"({"name":"Saga de Piccolo","unit":"CHAPTER","from":9,"to":30}]})");
        sidecar.close();
        QStringList files;
        for (int volume = 1; volume <= 3; ++volume) {
            QJsonArray chapters;
            for (int at = 0; at < 6; ++at) {
                const int number = (volume - 1) * 6 + at + 1;
                chapters.append(QJsonObject{{u"number"_s, number},
                                            {u"title"_s, u"Chapitre %1"_s.arg(number)},
                                            {u"startPage"_s, at * 16 + 2}});
            }
            const QString file = u"Tome %1.cbz"_s.arg(volume);
            files << file;
            QFile archive(series + u'/' + file);
            QVERIFY(archive.open(QIODevice::WriteOnly));
            archive.write(Zip::volume(
                QJsonDocument(QJsonObject{{u"leaf"_s, 1}, {u"number"_s, volume},
                                          {u"chapters"_s, chapters}})
                    .toJson(QJsonDocument::Compact)));
        }
        m_pretend->answerFor = [files](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Db", files, {});
            return QByteArray();
        };
        m_offers->offer({series});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        m_review->view()->open();
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
        m_tally = new ImportTally(m_imports, m_imports);
        m_review = new Review(m_imports);
    }

    /// A Komga series every file of which the library holds, announced with whatever
    /// `extra` adds to the server's answer — a declaration that differs, a creation.
    void aHeldSeriesAnnounced(const QByteArray &extra)
    {
        m_pretend->answerFor = [extra](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_h","root":"Blue Period","toSend":[],)"
                               R"("alreadyThere":)" + listed(komgaNames(1, 14))
                                   + R"(,"bytesToSend":0,)" + extra + '}');
        };
        m_offers->offer({aKomgaSeries()});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
    }


    /// An aimed card whose answer the test types: a series of two works' worth of volumes,
    /// the way down to it, and whatever `extra` adds (declarations, held volumes).
    static QByteArray held(const QByteArray &id, int number, int pages, int page = -1)
    {
        return "{\"entryId\":\"" + id + "\",\"number\":" + QByteArray::number(number)
               + ",\"file\":\"T" + QByteArray::number(number) + ".cbz\",\"size\":10,\"pages\":"
               + QByteArray::number(pages)
               + (page >= 0 ? ",\"page\":" + QByteArray::number(page) : QByteArray())
               + ",\"finished\":false,\"others\":0}";
    }

    static QByteArray match(const QString &path, const QByteArray &outcome,
                            const QByteArray &rest = {}, const QByteArray &arriving = "{}")
    {
        return "{\"path\":\"" + path.toUtf8() + "\",\"outcome\":\"" + outcome
               + "\",\"arriving\":" + arriving + (rest.isEmpty() ? QByteArray() : "," + rest) + "}";
    }

    void aimedAnswering(const QByteArray &kind, const QByteArray &id, const QByteArray &name,
                        const QByteArray &volumes, const QByteArray &absent,
                        const QByteArray &extra = {})
    {
        const QByteArray body =
            "{\"id\":\"imp_a\",\"root\":\"Blue Period\",\"creates\":[],\"toSend\":[],\"alreadyThere\":[],"
            "\"bytesToSend\":0,\"target\":{\"kind\":\"" + kind + "\",\"id\":\"" + id
            + "\",\"name\":\"" + name
            + "\",\"trail\":[{\"kind\":\"WORK\",\"id\":\"wk-1\",\"name\":\"Blue Period\",\"count\":1},"
              "{\"kind\":\"SERIES\",\"id\":\"ed-1\",\"name\":\"Édition française\",\"count\":12}"
            + (kind == "ENTRY" ? ",{\"kind\":\"ENTRY\",\"id\":\"e2\",\"name\":\"2\",\"count\":188}"
                               : QByteArray())
            + "]},\"volumes\":[" + volumes + "],\"absent\":[" + absent + "]" + (extra.isEmpty() ? QByteArray() : "," + extra) + "}";
        m_pretend->answerFor = [body](const QByteArray &request) {
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
    }

    /// The number the first card's sheet gives the file at `path`.
    double numberOf(const QString &path) const
    {
        for (const Facts::Sheet &sheet : m_imports->cards().at(0).road.sheets) {
            if (sheet.at != path)
                continue;
            for (const Facts::Field &field : sheet.fields) {
                if (field.key == u"number"_s)
                    return field.value.toDouble();
            }
        }
        return -1;
    }

    QVariantMap lineWith(const QString &id) const
    {
        for (const QVariant &one : m_review->rows()) {
            if (one.toMap().value(u"id"_s).toString() == id)
                return one.toMap();
        }
        return {};
    }

    /// A chapter aimed at: the volume it is in, the pages the chapter has and will have, and
    /// the reader's place `page` (counted from 0) with where it lands.
    void chapterAnswering(int page, int becomes, int to)
    {
        const QByteArray chapters =
            R"([{"id":"c5","number":5,"title":null,"from":0,"to":61,"becomesFrom":0,"becomesTo":61,)"
            R"("outcome":"KEEP"},{"id":"c6","number":6,"title":"Couleurs froides","from":62,)"
            R"("to":140,"becomesFrom":62,"becomesTo":)" + QByteArray::number(to)
            + R"(,"outcome":"REPLACE"},{"id":"c7","number":7,"title":null,"from":141,"to":187,)"
              R"("becomesFrom":)" + QByteArray::number(to + 1) + R"(,"becomesTo":)"
            + QByteArray::number(to + 47) + R"(,"outcome":"SHIFT"}])";
        const QByteArray body =
            R"({"id":"imp_c","root":"Blue Period","creates":[],"toSend":[],"alreadyThere":[],)"
            R"("bytesToSend":0,"target":{"kind":"CHAPTER","id":"c6","name":"Couleurs froides",)"
            R"("trail":[{"kind":"WORK","id":"wk-1","name":"Blue Period","count":1},)"
            R"({"kind":"SERIES","id":"ed-1","name":"Édition française","count":12},)"
            R"({"kind":"ENTRY","id":"e2","name":"2","count":188},)"
            R"({"kind":"CHAPTER","id":"c6","name":"Couleurs froides","count":79}]},)"
            R"("volumes":[],"absent":[)" + held("e2", 2, 188) + R"(],"splice":{"pagesBefore":188,)"
            R"("pagesAfter":)" + QByteArray::number(188 + to - 140) + R"(,"chapters":)" + chapters
            + R"(,"page":)" + QByteArray::number(page) + R"(,"pageBecomes":)"
            + QByteArray::number(becomes) + "}}";
        m_pretend->answerFor = [body](const QByteArray &request) {
            if (request.startsWith("DELETE "))
                return aReply(204, {});
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
        m_offers->reimport(u"CHAPTER"_s, u"c6"_s, u"Couleurs froides"_s,
                            {aKomgaSeries(1) + u"/"_s + komgaName(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// The ranges of the volume before and after, said at the volume, in the rail, in the
    /// list of its chapters and on the sheet — pages as a reader counts them, from 1.
    void a_chapter_reimport_shows_the_ranges_before_and_after()
    {
        chapterAnswering(111, 111, 123);

        QCOMPARE(m_review->title(), u"Remplacer un chapitre du tome 2"_s);
        QCOMPARE(m_review->importLabel(), u"Remplacer le chapitre"_s);
        QCOMPARE(m_review->footer(), u"L’archive du tome 2 est réécrite une fois."_s);
        const QVariantList rail = m_review->rail();
        QCOMPARE(rail.size(), 3 + 3);
        QCOMPARE(rail.at(3).toMap().value(u"name"_s).toString(),
                 u"5"_s);
        QVERIFY(!rail.at(2).toMap().value(u"selected"_s).toBool());
        QVERIFY(rail.at(4).toMap().value(u"selected"_s).toBool());

        const QVariantMap replaced = lineWith(u"c6"_s);
        QCOMPARE(replaced.value(u"detail"_s).toString(),
                 Words::pagesRangeChange(63, 141, 63, 124));
        QCOMPARE(replaced.value(u"chip"_s).toString(), u"Remplacer"_s);
        QCOMPARE(lineWith(u"c7"_s).value(u"chip"_s).toString(), u"Décalé de 17"_s);
        QCOMPARE(lineWith(u"c5"_s).value(u"chip"_s).toString(), u"Garder"_s);

        const QVariantMap splice = m_review->sheet().value(u"splice"_s).toMap();
        QCOMPARE(splice.value(u"current"_s).toMap().value(u"from"_s).toInt(), 63);
        QCOMPARE(splice.value(u"current"_s).toMap().value(u"to"_s).toInt(), 141);
        QCOMPARE(splice.value(u"extra"_s).toString(), u"17 pages en moins"_s);

        // Narrowing the range is asked for again, with the pages counted from 0.
        QVERIFY(m_review->edits()->setSplice(u"c6"_s, 63, 124));
        QCOMPARE(m_imports->cards().at(0).road.splice->from, 62);
        QCOMPARE(m_imports->cards().at(0).road.splice->to, 123);
    }

    /// A place past the end of the shorter chapter is brought back to its last page and the
    /// sentence says so; a place inside the pages that stay is kept, and says that too.
    void a_reader_beyond_the_new_end_of_the_chapter_is_told_where_they_land()
    {
        chapterAnswering(130, 123, 123);
        QVERIFY(m_review->sheet().value(u"splice"_s).toMap().value(u"note"_s).toString().endsWith(
            Words::spliceProgressBrought(131, 124)));
    }

    /// And a place inside the chapter that does not move is kept.
    void a_reader_inside_the_chapter_keeps_their_place()
    {
        chapterAnswering(111, 111, 123);
        QVERIFY(m_review->sheet().value(u"splice"_s).toMap().value(u"note"_s).toString().endsWith(
            Words::spliceProgressKept(112, u"6"_s)));
    }

    /// An universe of three series held (« Dragon Ball » identical, « Dragon Ball GT »
    /// absent from the drop, « Dragon Ball Super » at the root), and a drop of « Dragon Ball »,
    /// a new « Dragon Ball Z » and « Dragon Ball Super », which would move in.
    void universeAnswering()
    {
        const QByteArray body =
            R"({"id":"imp_u","root":"Dragon Ball","creates":[{"kind":"WORK","name":"Dragon Ball Z",)"
            R"("at":"Dragon Ball Z"}],"toSend":[],"alreadyThere":[],"bytesToSend":0,)"
            R"("moves":[{"workId":"w-super","name":"Dragon Ball Super","from":"",)"
            R"("at":"Dragon Ball Super","matched":"TITLE"}],)"
            R"("target":{"kind":"UNIVERSE","id":"u1","name":"Dragon Ball","trail":[)"
            R"({"kind":"UNIVERSE","id":"u1","name":"Dragon Ball","count":3}]},)"
            R"("volumes":[)" + match(u"Dragon Ball/Tome 01.cbz"_s.toUtf8(), "SAME") + ","
            + match(u"Dragon Ball/Tome 02.cbz"_s.toUtf8(), "SAME") + ","
            + match(u"Dragon Ball Z/Tome 01.cbz"_s.toUtf8(), "ADD") + ","
            + match(u"Dragon Ball Super/Tome 01.cbz"_s.toUtf8(), "REPLACE",
                    R"("number":1,"differs":["pages"])")
            + R"(],"absent":[],"absentSeries":[{"workId":"w-gt","seriesId":"s-gt",)"
              R"("name":"Dragon Ball GT","volumes":1}]})";
        m_pretend->answerFor = [body](const QByteArray &request) {
            if (request.startsWith("DELETE "))
                return aReply(204, {});
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
        m_offers->reimport(u"UNIVERSE"_s, u"u1"_s, u"Dragon Ball"_s,
                            {aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// Each series of an universe is a line, said by what becomes of it: moved, added, changed,
    /// kept, or identical and folded.
    void a_universe_reimport_lists_its_series_by_what_happens_to_them()
    {
        universeAnswering();

        QCOMPARE(m_review->title(), u"Réimporter Dragon Ball"_s);
        QCOMPARE(m_review->subtitle(),
                 u"Univers · 3 séries dans la bibliothèque · 3 dans l’apport"_s);
        QCOMPARE(m_review->listTitle(), u"Séries"_s);
        QCOMPARE(m_review->railNote(),
                 u"Aucune série n’est retirée de l’univers sans que vous le demandiez."_s);
        QCOMPARE(m_review->footer(), u"Les séries identiques ne sont ni lues ni renvoyées."_s);

        const QVariantMap moved = lineWith(u"Dragon Ball Super"_s);
        QCOMPARE(moved.value(u"chip"_s).toString(), u"Déplacer"_s);
        QCOMPARE(moved.value(u"detail"_s).toString(),
                 u"à la racine, 1 → 1 tomes dans l’univers"_s);
        const QVariantMap added = lineWith(u"Dragon Ball Z"_s);
        QCOMPARE(added.value(u"chip"_s).toString(), u"Ajouter"_s);
        QCOMPARE(added.value(u"detail"_s).toString(), u"nouvelle série · 1 tome"_s);

        // Identical series are folded under their header until asked for.
        QVERIFY(lineWith(u"Dragon Ball"_s).isEmpty());
        // A creation is one change, not that and each of its volumes.
        QCOMPARE(m_review->importLabel(), Words::applyChanges(2));
    }

    /// A series the library holds and the drop does not bring is said, kept, and removed by
    /// nothing: not ticked, not counted, not a reason to stop.
    void a_series_absent_from_the_universe_drop_is_kept()
    {
        universeAnswering();
        m_review->view()->toggleIdentical();

        const QVariantMap kept = lineWith(u"absent:w-gt"_s);
        QCOMPARE(kept.value(u"chip"_s).toString(), u"Garder"_s);
        QCOMPARE(kept.value(u"detail"_s).toString(), u"absente de l’apport · 1 tome"_s);
        QVERIFY(!kept.value(u"checkable"_s).toBool());
        QCOMPARE(kept.value(u"group"_s).toInt(), int(Review::Filter::Identical));
        // The two changes counted are the creation and the replaced volume: never the absent.
        QCOMPARE(m_review->importLabel(), Words::applyChanges(2));
    }

    /// A series the library holds elsewhere is offered to be moved in before anything else:
    /// the move is the recommended choice, a second series the other, and choosing the first
    /// ticks the move.
    void a_series_held_elsewhere_is_offered_to_move_first()
    {
        universeAnswering();
        m_review->view()->select(u"Dragon Ball Super"_s);

        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"alarming"_s).toMap().value(u"title"_s).toString(),
                 u"Elle existe déjà, à la racine."_s);
        QCOMPARE(sheet.value(u"alarming"_s).toMap().value(u"text"_s).toString(),
                 u"On la déplace dedans avec ses lectures, au lieu d’en créer une seconde."_s);
        const QVariantList choices = sheet.value(u"choices"_s).toList();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices.constFirst().toMap().value(u"label"_s).toString(),
                 u"Déplacer dans l’univers"_s);
        QCOMPARE(choices.constFirst().toMap().value(u"badge"_s).toString(), u"Conseillé"_s);
        QCOMPARE(choices.constLast().toMap().value(u"label"_s).toString(),
                 u"Créer une seconde série"_s);
        QVERIFY(!choices.constFirst().toMap().value(u"chosen"_s).toBool());

        QVERIFY(m_review->edits()->choose(u"Dragon Ball Super"_s, u"move"_s));
        // Announced again to carry the identity the move is made by, and the tick stays.
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(m_imports->cards().at(0).road.filing.contains(u"w-super"_s));
        QVERIFY(m_review->sheet().value(u"choices"_s).toList().constFirst().toMap()
                    .value(u"chosen"_s).toBool());
        QVERIFY(m_review->edits()->choose(u"Dragon Ball Super"_s, u"second"_s));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(!m_imports->cards().at(0).road.filing.contains(u"w-super"_s));
    }

    /// A series whose title is one the library holds is met before it is created: its line
    /// says it exists already, its sheet offers to reimport onto the one held or to create a
    /// second, and the second stops being asked about.
    void a_series_that_looks_like_one_held_is_met_before_it_is_created()
    {
        const QByteArray body =
            R"({"id":"imp_l","root":"Blue Period","creates":[{"kind":"WORK","name":"Blue Period",)"
            R"("at":""}],"resembles":[{"at":"","seriesId":"s1","name":"Blue Period","from":""}],)"
            R"("toSend":[],"alreadyThere":[],"bytesToSend":0})";
        m_pretend->answerFor = [body](const QByteArray &request) {
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
        m_offers->offer({aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QString id;
        for (const QVariant &one : m_review->rows()) {
            if (one.toMap().value(u"chip"_s).toString() == u"Existe déjà"_s)
                id = one.toMap().value(u"id"_s).toString();
        }
        QVERIFY(!id.isEmpty());
        m_review->view()->select(id);
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"alarming"_s).toMap().value(u"title"_s).toString(),
                 u"« "_s + u"Blue Period"_s + u" » est déjà dans la bibliothèque."_s);
        const QVariantList choices = sheet.value(u"choices"_s).toList();
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices.at(1).toMap().value(u"label"_s).toString(),
                 Words::newEditionOf(u"Blue Period"_s));
        QCOMPARE(choices.constFirst().toMap().value(u"label"_s).toString(),
                 u"Réimporter sur « "_s + u"Blue Period"_s + u" »"_s);
        QCOMPARE(choices.constLast().toMap().value(u"label"_s).toString(),
                 u"Créer une seconde série"_s);

        QVERIFY(m_review->edits()->choose(id, u"second"_s));
        for (const QVariant &one : m_review->rows())
            QVERIFY(one.toMap().value(u"chip"_s).toString() != u"Existe déjà"_s);
    }

    /// The folder can be another edition of the series held: choosing it aims the card at the
    /// series with the kind that adds editions, as « Ajouter des éditions… » does.
    void a_series_that_looks_like_one_held_can_become_one_of_its_editions()
    {
        const QByteArray body =
            R"({"id":"imp_e","root":"Blue Period","creates":[{"kind":"WORK","name":"Blue Period",)"
            R"("at":""}],"resembles":[{"at":"","seriesId":"s1","name":"Blue Period","from":""}],)"
            R"("toSend":[],"alreadyThere":[],"bytesToSend":0})";
        m_pretend->answerFor = [body](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
        m_offers->offer({aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QString id;
        for (const QVariant &one : m_review->rows()) {
            if (one.toMap().value(u"chip"_s).toString() == u"Existe déjà"_s)
                id = one.toMap().value(u"id"_s).toString();
        }
        QVERIFY(!id.isEmpty());

        QVERIFY(m_review->edits()->choose(id, u"edition"_s));

        const Card &card = m_imports->cards().constFirst();
        QVERIFY(card.road.target.has_value());
        QCOMPARE(card.road.target->kind, u"SERIES_NEW_EDITIONS"_s);
        QCOMPARE(card.road.target->id, u"s1"_s);
    }

    /// The sheet of a series says where the server will file it, in place of where its values
    /// came from, and two files that would meet are said over it.
    void the_subtitle_of_a_series_says_where_it_will_be_filed()
    {
        const QByteArray body =
            R"({"id":"imp_f","root":"Blue Period","creates":[{"kind":"WORK","name":"Blue Period",)"
            R"("at":""}],"landsAs":[{"path":"Tome 1.cbz","folder":"Blue Period › Originale",)"
            R"("file":"blue-period_tome_001.cbz","wasCalled":"Tome 1.cbz"}],)"
            R"("collisions":[{"path":"a.cbz","with":"b.cbz"}],)"
            R"("toSend":[],"alreadyThere":[],"bytesToSend":0})";
        m_pretend->answerFor = [body](const QByteArray &request) {
            return request.startsWith("POST /import ") ? aReply(200, body) : QByteArray();
        };
        m_offers->offer({aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        // The series is one of the rows; the others (its volumes) say what they are.
        QVariantMap sheet;
        for (const QVariant &row : m_review->rows()) {
            m_review->view()->select(row.toMap().value(u"id"_s).toString());
            sheet = m_review->sheet();
            if (sheet.value(u"subtitle"_s).toString().startsWith(u"Sera rangé"_s))
                break;
        }
        QCOMPARE(sheet.value(u"subtitle"_s).toString(),
                 u"Sera rangé sous Blue Period › Originale"_s);
        bool said = false;
        for (const QVariant &note : sheet.value(u"notes"_s).toList())
            said = said || note.toMap().value(u"text"_s).toString().contains(u"b.cbz"_s);
        QVERIFY(said);
    }

    /// What the server found of a series of six volumes dropped on the one held: two the
    /// same, two replaced, two new, and the eleventh not brought.
    void aSeriesReimport()
    {
        const QString one = komgaName(1), two = komgaName(2), three = komgaName(3),
                      four = komgaName(4), five = komgaName(5), six = komgaName(6);
        aimedAnswering(
            "SERIES", "ed-1", "Édition française",
            match(one, "SAME") + "," + match(two, "SAME") + ","
                + match(three, "REPLACE",
                        R"("number":3,"present":)" + held("e3", 3, 196, 111)
                            + R"(,"differs":["pages"])",
                        R"({"pages":200})")
                + "," + match(four, "UPDATE", R"("number":4,"differs":["title"],"present":)"
                                                  + held("e4", 4, 190))
                + "," + match(five, "ADD", R"("number":5)") + "," + match(six, "ADD"),
            held("e11", 11, 180));
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Édition française"_s,
                            {aKomgaSeries(6)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// A drop aimed at a series is read as what it replaces, adds and leaves, with the way
    /// down to it beside: the volumes the server compared, and nothing of the folder's own
    /// tree.
    void a_series_reimport_shows_its_target_and_what_changes()
    {
        aSeriesReimport();

        QCOMPARE(m_review->title(), u"Réimporter Blue Period"_s);
        QCOMPARE(m_review->subtitle(),
                 u"Série · Édition française · 12 tomes dans la bibliothèque"_s);
        QCOMPARE(m_review->railTitle(), u"Cible"_s);
        const QVariantList rail = m_review->rail();
        QCOMPARE(rail.size(), 2);
        QCOMPARE(rail.constLast().toMap().value(u"name"_s).toString(), u"Édition française"_s);
        QVERIFY(rail.constLast().toMap().value(u"selected"_s).toBool());
        QCOMPARE(rail.constFirst().toMap().value(u"count"_s).toInt(), 1);
        QCOMPARE(m_review->railNote(), Words::aimedRailNoteSeries());
        QCOMPARE(m_review->note(), u"2 ajoutés · 2 remplacés · 1 absent"_s);
        QCOMPARE(m_review->listTitle(), u"Tomes qui changent"_s);

        const QVariantList counts = m_review->counts();
        QCOMPARE(counts.at(0).toMap().value(u"count"_s).toInt(), 7);
        QCOMPARE(counts.at(1).toMap().value(u"count"_s).toInt(), 3);
        QCOMPARE(counts.at(2).toMap().value(u"count"_s).toInt(), 2);
        QCOMPARE(counts.at(3).toMap().value(u"count"_s).toInt(), 2);

        const QVariantMap replaced = lineWith(komgaName(3));
        QCOMPARE(replaced.value(u"chip"_s).toString(), u"Remplacer"_s);
        QCOMPARE(replaced.value(u"detail"_s).toString(),
                 u"196 → 200 pages · progression gardée"_s);
        QVERIFY(replaced.value(u"switchable"_s).toBool());
        QVERIFY(replaced.value(u"on"_s).toBool());
        QCOMPARE(lineWith(komgaName(5)).value(u"chip"_s).toString(), u"Ajouter"_s);
        // Ticked: two replacements and two additions.
        QCOMPARE(m_review->importLabel(), Words::applyChanges(4));
        QCOMPARE(m_review->footer(), u"Les interrupteurs choisissent ce qui est appliqué."_s);
    }

    /// A series never loses a volume without being asked: the one the drop does not bring is
    /// said, kept, and has no switch to turn.
    void a_volume_absent_from_the_drop_is_kept_and_cannot_be_switched()
    {
        aSeriesReimport();

        const QVariantMap line = lineWith(u"absent:e11"_s);
        QCOMPARE(line.value(u"chip"_s).toString(), u"Garder"_s);
        QCOMPARE(line.value(u"detail"_s).toString(), u"absent de l’apport"_s);
        QVERIFY(!line.value(u"switchable"_s).toBool());
        m_review->view()->select(u"absent:e11"_s);
        const QVariantMap alarming = m_review->sheet().value(u"alarming"_s).toMap();
        QCOMPARE(alarming.value(u"title"_s).toString(), u"Le tome 11 n’est pas dans l’apport."_s);
    }

    /// A file that is the tome 5 dropped on the tome 2: its own place comes first, and says
    /// the volume aimed at stays intact.
    void the_file_of_another_volume_offers_its_own_place_first()
    {
        aimedAnswering("ENTRY", "e2", "Tome 2", match(komgaName(5), "OTHER_NUMBER", R"("number":5)"),
                       held("e2", 2, 188));
        m_offers->reimport(u"ENTRY"_s, u"e2"_s, u"Tome 2"_s,
                            {aKomgaSeries(5) + u"/"_s + komgaName(5)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QCOMPARE(m_review->title(), u"Réimporter le tome 2"_s);
        QCOMPARE(m_review->note(), u"Cible : le tome 2"_s);
        QCOMPARE(m_review->railNote(), Words::aimedRailNoteVolume());
        QCOMPARE(m_review->listTitle(), u"Ce que vous apportez"_s);
        m_review->view()->select(komgaName(5));
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"alarming"_s).toMap().value(u"title"_s).toString(),
                 u"Ce fichier est le tome 5, pas le tome 2."_s);
        const QVariantList choices = sheet.value(u"choices"_s).toList();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices.constFirst().toMap().value(u"id"_s).toString(), u"own"_s);
        QCOMPARE(choices.constFirst().toMap().value(u"label"_s).toString(),
                 u"Ranger au tome 5"_s);
        QCOMPARE(choices.constFirst().toMap().value(u"badge"_s).toString(), u"Conseillé"_s);
        QCOMPARE(choices.constLast().toMap().value(u"label"_s).toString(),
                 u"Remplacer quand même le tome 2"_s);
        QCOMPARE(m_review->importLabel(), u"Ranger au tome 5"_s);
    }

    /// A reimport writes nothing of what is identical, so the frame names what the library holds
    /// of the target — the series, and the volume when one is aimed at — for the windows that
    /// correct them. A drop that aims at nothing names neither.
    void the_aimed_frame_names_what_the_library_holds_to_be_corrected()
    {
        aSeriesReimport();
        QCOMPARE(m_review->sheet().value(u"editSeriesId"_s).toString(), u"ed-1"_s);
        QVERIFY(m_review->sheet().value(u"editEntryId"_s).toString().isEmpty());
    }

    void the_frame_of_a_volume_reimport_names_the_volume_and_its_series()
    {
        aimedAnswering("ENTRY", "e2", "Tome 2", match(komgaName(5), "OTHER_NUMBER", R"("number":5)"),
                       held("e2", 2, 188));
        m_offers->reimport(u"ENTRY"_s, u"e2"_s, u"Tome 2"_s,
                            {aKomgaSeries(5) + u"/"_s + komgaName(5)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"editEntryId"_s).toString(), u"e2"_s);
        QCOMPARE(sheet.value(u"editSeriesId"_s).toString(), u"ed-1"_s);
    }

    void a_classic_drop_names_nothing_to_correct()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();
        QVERIFY(!m_review->sheet().contains(u"editSeriesId"_s));
    }

    /// Replacing the aimed volume anyway is a correction of the file's number to the aimed
    /// one; putting it at its own place gives it back the number it reads.
    void choosing_to_replace_the_aimed_volume_corrects_its_number()
    {
        aimedAnswering("ENTRY", "e2", "Tome 2", match(komgaName(5), "OTHER_NUMBER", R"("number":5)"),
                       held("e2", 2, 188));
        m_offers->reimport(u"ENTRY"_s, u"e2"_s, u"Tome 2"_s,
                            {aKomgaSeries(5) + u"/"_s + komgaName(5)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QVERIFY(m_review->edits()->choose(komgaName(5), u"aimed"_s));
        QCOMPARE(numberOf(komgaName(5)), 2.0);
        QVERIFY(m_review->edits()->choose(komgaName(5), u"own"_s));
        QCOMPARE(numberOf(komgaName(5)), 5.0);
        QVERIFY(!m_review->edits()->choose(komgaName(5), u"nowhere"_s));
    }

    /// Four pages more is a corrected scan and the place is kept without a word; thirty-eight
    /// fewer is another book and the profile is asked.
    void a_much_shorter_volume_asks_about_the_progress()
    {
        const QByteArray shorter = R"("arriving":{"pages":150})";
        const QByteArray longer = R"("arriving":{"pages":200})";
        aimedAnswering(
            "SERIES", "ed-1", "Édition française",
            R"({"path":")" + komgaName(1).toUtf8() + R"(","outcome":"REPLACE","number":1,)"
                + shorter + R"(,"differs":["pages"],"present":)" + held("e1", 1, 188, 100) + "},"
                + R"({"path":")" + komgaName(2).toUtf8() + R"(","outcome":"REPLACE","number":2,)"
                + longer + R"(,"differs":["pages"],"present":)" + held("e2", 2, 196, 100) + "}",
            "");
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Édition française"_s, {aKomgaSeries(2)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        m_review->view()->select(komgaName(1));
        const QVariantMap far = m_review->sheet().value(u"progress"_s).toMap();
        QVERIFY(far.value(u"ask"_s).toBool());
        QCOMPARE(far.value(u"text"_s).toString(),
                 u"Votre progression appartient au tome 1 : elle ne bouge pas."_s);
        QVERIFY(!far.value(u"reset"_s).toBool());
        QVERIFY(m_review->edits()->setReset(komgaName(1), true));
        QVERIFY(m_review->sheet().value(u"progress"_s).toMap().value(u"reset"_s).toBool());
        QVERIFY(m_imports->cards().at(0).road.resetting.contains(komgaName(1)));

        m_review->view()->select(komgaName(2));
        QVERIFY(!m_review->sheet().value(u"progress"_s).toMap().value(u"ask"_s).toBool());
    }

    /// A value kept is one change the drop no longer makes: the label and the sheet's count
    /// both go down by one.
    void keeping_the_library_value_counts_one_change_less()
    {
        aimedAnswering("SERIES", "ed-1", "Édition française", match(komgaName(1), "ADD"), "",
                       R"("declarations":[{"path":"work.json","differs":["publisher","title"],)"
                       R"("present":{"publisher":"Pika"}}])");
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Édition française"_s, {aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        // The addition and two values of the declaration.
        QCOMPARE(m_review->importLabel(), Words::applyChanges(3));
        m_review->view()->select(u"all"_s);
        QCOMPARE(m_review->sheet().value(u"subtitle"_s).toString(), u"2 infos modifiées"_s);
        const QVariantList fields = m_review->sheet().value(u"fields"_s).toList();
        QCOMPARE(fields.constFirst().toMap().value(u"present"_s).toString(), u"Pika"_s);
        QCOMPARE(fields.constLast().toMap().value(u"present"_s).toString(), u"Non renseigné"_s);

        QVERIFY(m_review->edits()->setKeep(u"work.json"_s, u"publisher"_s, true));
        QCOMPARE(m_review->importLabel(), Words::applyChanges(2));
        QCOMPARE(m_review->sheet().value(u"subtitle"_s).toString(), u"1 info modifiée"_s);
    }

    /// A file that says it belongs to another series is said so, in the alert's words, and
    /// its switch starts off: it would land on a volume of something else.
    void another_series_is_said_and_starts_unticked()
    {
        aimedAnswering("SERIES", "ed-1", "Édition française",
                       match(komgaName(1), "OTHER_SERIES", R"("number":1)"), "");
        m_offers->reimport(u"SERIES"_s, u"ed-1"_s, u"Édition française"_s, {aKomgaSeries(1)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        const QVariantMap line = lineWith(komgaName(1));
        QCOMPARE(line.value(u"chip"_s).toString(), u"Autre série"_s);
        QCOMPARE(line.value(u"detail"_s).toString(),
                 u"dit appartenir à « Blue Period »"_s);
        QVERIFY(line.value(u"switchable"_s).toBool());
        QVERIFY(!line.value(u"on"_s).toBool());
        m_review->view()->select(komgaName(1));
        QCOMPARE(m_review->sheet().value(u"alarming"_s).toMap().value(u"title"_s).toString(),
                 u"Ce fichier dit appartenir à « Blue Period »."_s);
        QVERIFY(!m_review->importable());
    }

    /// Non-regression: a drop that aims at nothing keeps the frame it always had.
    void a_drop_without_target_keeps_its_frame()
    {
        aHeldSeriesAnnounced(R"("creates":[])");

        QVERIFY(m_review->title().isEmpty());
        QVERIFY(m_review->railTitle() != u"Cible"_s);
        QVERIFY(m_review->rail().size() >= 1);
        QVERIFY(m_review->listTitle() != u"Tomes qui changent"_s);
    }

    void cleanup()
    {
        delete m_review;
        delete m_imports;
        delete m_server;
        delete m_settings;
        delete m_pretend;
        // Each test drops its own folders: one left from the test before would be walked
        // again under the same name.
        QDir(m_folder.path()).removeRecursively();
        QDir().mkpath(m_folder.path());
    }

    /// The frame opens on what asks for a look, and on the container before its files: the
    /// reading direction is said once for the series, and opening on a volume first would
    /// show fourteen sheets that cannot fix it.
    void the_frame_opens_on_the_first_thing_to_review()
    {
        // Volume 14 is to review too — its number only guessed — so a frame that looked at
        // files first would open on « Tome 14 ».
        aKomgaSeriesAnnounced(12, 14);
        m_review->view()->open();

        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Blue Period"_s);
        QVERIFY(fieldOnSheet(u"readingDirection"_s).value(u"lacking"_s).toBool());
        QVERIFY(!fieldOnSheet(u"title"_s).value(u"lacking"_s).toBool());
        QCOMPARE(m_review->sheet().value(u"subtitle"_s).toString(),
                 u"Série · lue dans ComicInfo.xml"_s);
        QCOMPARE(m_review->subtitle(), u"1 série · 14 fichiers · ComicInfo.xml"_s);
        QCOMPARE(m_review->listTitle(), Words::volumesOf(u"Blue Period"_s));
        QCOMPARE(m_review->note(), Words::alreadyHeld(u"Blue Period"_s, 12, 14));
        QVERIFY(m_review->sheet().value(u"editable"_s).toBool());
        QVERIFY(fieldOnSheet(u"title"_s).value(u"editable"_s).toBool());
        // The one sentence that says why a missing direction stops the step, said loudly.
        const QVariantList notes = m_review->sheet().value(u"notes"_s).toList();
        QVERIFY(std::ranges::any_of(notes, [](const QVariant &one) {
            return one.toMap().value(u"text"_s).toString() == Words::readingDirectionUnsaid()
                   && one.toMap().value(u"alarming"_s).toBool();
        }));
        // A missing field is not a blank one: it carries the words a screen shows in it.
        QCOMPARE(fieldOnSheet(u"volumeCount"_s).value(u"placeholder"_s).toString(),
                 Words::notFilledIn());
        QCOMPARE(fieldOnSheet(u"readingDirection"_s).value(u"choices"_s).toList().size(), 3);
        QCOMPARE(fieldOnSheet(u"status"_s).value(u"choices"_s).toList().size(), 4);
    }

    /// A series that says everything has nothing to open on but itself — not « Tout
    /// l’apport », which for one series is one line to click before seeing anything.
    void a_lone_complete_series_opens_on_the_series()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Koro", {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s},
                                 {}, R"([{"kind":"WORK","name":"Koro Quest","at":""}])");
            return QByteArray();
        };
        m_offers->offer({aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->open();

        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Koro Quest"_s);
        const QVariantList rail = m_review->rail();
        const auto selected = std::ranges::find_if(rail, [](const QVariant &one) {
            return one.toMap().value(u"selected"_s).toBool();
        });
        QVERIFY(selected != rail.end());
        QCOMPARE(selected->toMap().value(u"id"_s).toString(), idOf(0, {}));
        QCOMPARE(lines().size(), 3);
        QCOMPARE(m_review->railNote(), Words::everythingSaid());
    }

    /// The counters count the list that is shown, not the whole drop: fourteen volumes, of
    /// which twelve the library holds and two ready — the one whose number is read off its
    /// name included, which asked for a look when « Deviné » was a question.
    void the_counters_count_what_the_list_holds()
    {
        aKomgaSeriesAnnounced(12, 14);
        m_review->view()->open();
        m_review->view()->select(idOf(0, {}));

        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 14);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 12);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 0);
        QCOMPARE(countOf(Review::Filter::Ready).value(u"count"_s).toInt(), 2);
        QCOMPARE(countOf(Review::Filter::All).value(u"label"_s).toString(),
                 Words::reviewFilter(Words::Reviewed::All));
        QVERIFY(countOf(Review::Filter::All).value(u"lit"_s).toBool());
        QVERIFY(!countOf(Review::Filter::Identical).value(u"lit"_s).toBool());
    }

    /// Identical volumes are folded into one line that says how many and what they spare;
    /// unfolded, they are all there, and their header says how to fold them again.
    void identical_volumes_are_folded_with_the_bytes_they_spare()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();
        qint64 spared = 0;
        for (const QString &name : komgaNames(1, 12))
            spared += QFileInfo(m_folder.filePath(u"Blue Period/"_s + name)).size();

        QVERIFY2(headers().contains(Words::identicalGroup(12, spared)),
                 qPrintable(headers().join(u" | "_s)));
        QCOMPARE(lines().size(), 2);
        QVERIFY(std::ranges::none_of(lines(), [](const QVariantMap &one) {
            return one.value(u"group"_s).toInt() == int(Review::Filter::Identical);
        }));

        QCOMPARE(identicalHeader().value(u"action"_s).toString(), Words::showIt());

        m_review->view()->toggleIdentical();
        QCOMPARE(lines().size(), 14);
        QCOMPARE(identicalHeader().value(u"action"_s).toString(), Words::hideIt());
    }

    /// A filter keeps its own rows and nothing else — the identical ones included, which a
    /// filter on « Identiques » shows even though the group is folded.
    void a_filter_keeps_only_its_rows()
    {
        aKomgaSeriesAnnounced(12, 14);
        m_review->view()->open();

        m_review->view()->setFilter(int(Review::Filter::Ready));
        QCOMPARE(lines().size(), 2);
        QCOMPARE(lines().constFirst().value(u"name"_s).toString(), u"Tome 13"_s);
        QCOMPARE(headers(), QStringList{Words::reviewGroup(Words::Reviewed::Ready, 2)});
        QVERIFY(countOf(Review::Filter::Ready).value(u"lit"_s).toBool());
        // The counters still count the whole list: a filter that zeroed the others would
        // hide what it hides.
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 12);

        m_review->view()->setFilter(int(Review::Filter::Identical));
        QCOMPARE(lines().size(), 12);

        // Invokable as well as written: a `.qml` file calls it from a button's `onClicked`.
        QVERIFY(QMetaObject::invokeMethod(m_review->view(), "setFilter",
                                          Q_ARG(int, int(Review::Filter::ToReview))));
        QCOMPARE(m_review->view()->filter(), int(Review::Filter::ToReview));
        // Nothing to look at: a number read off a name is shown, not asked about.
        QCOMPARE(lines().size(), 0);
    }

    /// A series dropped beside a universe is not hung under it: it has a node of its own,
    /// « Sans univers », after the universe and its series.
    void the_rail_puts_a_series_without_universe_under_its_own_node()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            if (request.contains("\"root\":\"Terres\""))
                return anOpening("Terres", {u"Elfes/Tome 1.cbz"_s, u"Mages/Tome 1.cbz"_s}, {});
            return anOpening("Blue Period", komgaNames(13, 14), komgaNames(1, 12));
        };
        m_offers->offer({aUniverse()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_offers->offer({aKomgaSeries()});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Reviewing));

        QCOMPARE(railIds(), (QStringList{u"all"_s, idOf(0, {}), idOf(0, u"Elfes"_s),
                                         idOf(0, u"Mages"_s), u"loose"_s, idOf(1, {})}));
        const QVariantList rail = m_review->rail();
        QCOMPARE(rail.at(4).toMap().value(u"name"_s).toString(), Words::withoutUniverse());
        QCOMPARE(rail.at(4).toMap().value(u"depth"_s).toInt(), 1);
        QCOMPARE(rail.at(5).toMap().value(u"depth"_s).toInt(), 2);
        QCOMPARE(rail.at(1).toMap().value(u"name"_s).toString(), u"Terres d’Arran"_s);
        QCOMPARE(rail.at(1).toMap().value(u"count"_s).toInt(), 2);
        QCOMPARE(rail.at(0).toMap().value(u"count"_s).toInt(), 3);
        QCOMPARE(m_review->subtitle(),
                 Words::apportSummary(1, 3, 16, Words::Source::Several));

        // Folded, a universe hides its series and keeps its own line.
        m_review->view()->toggle(idOf(0, {}));
        QCOMPARE(railIds(), (QStringList{u"all"_s, idOf(0, {}), u"loose"_s, idOf(1, {})}));
        QVERIFY(rail.at(1).toMap().value(u"expanded"_s).toBool());
        QVERIFY(!m_review->rail().at(1).toMap().value(u"expanded"_s).toBool());
        QVERIFY(m_review->rail().at(1).toMap().value(u"expandable"_s).toBool());

        m_review->view()->select(idOf(0, {}));
        QCOMPARE(m_review->listTitle(), Words::seriesOfTheUniverse());
        QCOMPARE(lines().size(), 2);
    }

    /// A value typed on the sheet reaches the card's own sheet, said: the sheet is a view
    /// of the card, and a correction kept in the view alone would never be announced.
    void an_edit_from_the_sheet_reaches_the_card()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();

        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"title"_s, u"Ao"_s));
        const Facts::Field title = fieldOnCard(0, {}, u"title"_s);
        QCOMPARE(title.value.toString(), u"Ao"_s);
        QCOMPARE(title.origin, Facts::Origin::Said);
        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Ao"_s);

        // A list is given as words: typed with commas, « Dupont, Jean » was two people.
        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"genres"_s, u"Seinen, Art"_s));
        QVERIFY(m_review->edits()->editWords(idOf(0, {}), u"genres"_s, {u"Seinen"_s, u" Art "_s, u""_s}));
        QCOMPARE(fieldOnCard(0, {}, u"genres"_s).value.toArray(),
                 (QJsonArray{u"Seinen"_s, u"Art"_s}));
        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QCOMPARE(fieldOnCard(0, {}, u"readingDirection"_s).value.toString(),
                 u"RIGHT_TO_LEFT"_s);
        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"volumeCount"_s, u"16"_s));
        QCOMPARE(fieldOnCard(0, {}, u"volumeCount"_s).value.toInt(), 16);
    }

    /// A word given twice is kept once, as the server keeps a list: « arleston » beside
    /// « Arleston » made a second pill and a second author of one person.
    void a_word_given_twice_is_kept_once()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();

        QVERIFY(m_review->edits()->editWords(idOf(0, {}), u"genres"_s,
                                    {u"Arleston"_s, u"arleston"_s, u"Arlestón"_s, u"Art"_s}));
        QCOMPARE(fieldOnCard(0, {}, u"genres"_s).value.toArray(),
                 (QJsonArray{u"Arleston"_s, u"Art"_s}));
    }

    /// A number that does not read as one changes nothing, and says why under the field:
    /// written as it was typed, « douze » would leave the volume without a place.
    void a_number_that_does_not_read_changes_nothing_and_says_why()
    {
        aKomgaSeriesAnnounced();
        const QString thirteen = idOf(0, komgaName(13));
        m_review->view()->select(thirteen);

        QVERIFY(!m_review->edits()->edit(thirteen, u"number"_s, u"douze"_s));
        QCOMPARE(fieldOnCard(0, komgaName(13), u"number"_s).value.toDouble(), 13.0);
        QCOMPARE(fieldOnCard(0, komgaName(13), u"number"_s).origin, Facts::Origin::Taken);
        QCOMPARE(fieldOnSheet(u"number"_s).value(u"doubt"_s).toString(),
                 Words::notANumber(u"douze"_s, false));

        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"volumeCount"_s, u"3,5"_s));
        QCOMPARE(fieldOnCard(0, {}, u"volumeCount"_s).origin, Facts::Origin::Missing);

        // A comma is how a French reader writes a half: it reads, and the doubt goes.
        QVERIFY(m_review->edits()->edit(thirteen, u"number"_s, u"12,5"_s));
        QCOMPARE(fieldOnCard(0, komgaName(13), u"number"_s).value.toDouble(), 12.5);
        QCOMPARE(fieldOnSheet(u"number"_s).value(u"doubt"_s).toString(), QString());
    }

    /// The footer counts what still stops the step and what will be rewritten, and neither
    /// counts the twelve volumes that will not travel.
    void the_footer_counts_what_is_missing_and_what_will_be_rewritten()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();

        // The direction nobody said holds the series back, and is counted as what is missing.
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' '
                     + u"1 valeur manque encore · 2 archives seront réécrites"_s);
        QCOMPARE(m_review->importLabel(), Words::importVolumes(0));
        QVERIFY(!m_review->importable());
        QVERIFY(!m_review->nothingToImport());
        QCOMPARE(m_review->railNote(), Words::comicInfoConverted(2));

        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 2));
        QCOMPARE(m_review->importLabel(), Words::importVolumes(2));
        QVERIFY(m_review->importable());
    }

    /// Everything identical is « Rien à importer », and nothing to press: the step never
    /// closes by itself, and a button that sent nothing would say it did something.
    void everything_identical_says_there_is_nothing_to_import()
    {
        aKomgaSeriesAnnounced(14);
        m_review->view()->open();

        QVERIFY(m_review->nothingToImport());
        QCOMPARE(m_review->importLabel(), Words::nothingToImport());
        QVERIFY(!m_review->importable());
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
    }

    /// What is identical is something the library holds of the drop, and a reader who dropped it
    /// is owed the sight of it: the cards' button goes on to the frame for that, and closes
    /// only on a drop of which the library holds nothing. A drop that goes, or stops to be
    /// looked at, of which the library holds nothing, has nothing spared.
    void a_drop_the_library_already_holds_says_it_spares_something()
    {
        aKomgaSeriesAnnounced(14);
        m_review->view()->open();
        QVERIFY(m_review->nothingToImport());
        QVERIFY(m_review->sparesSomething());
    }

    void a_drop_of_which_the_library_holds_nothing_spares_nothing()
    {
        aKomgaSeriesAnnounced(0);
        m_review->view()->open();
        QVERIFY(!m_review->nothingToImport());
        QVERIFY(!m_review->sparesSomething());
    }

    /// The button counts what « Importer » sends now, and not what went before it: a folder
    /// already on its way stays in the queue, and dropping a second one of three volumes
    /// said « Importer 6 tomes ».
    void what_is_already_on_its_way_is_not_counted_again()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            const QStringList three{u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s};
            if (request.startsWith("POST /import ") && request.contains("\"root\":\"Koro\""))
                return anOpening("Koro", three, {});
            if (request.startsWith("POST /import "))
                return anOpening("Mira", three, {});
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));

        m_offers->offer({aCompleteSeries(u"Mira"_s)});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(m_review->importLabel(), Words::importVolumes(3));
    }

    /// A file dropped alone beside a folder goes with it at « Importer », and the button
    /// counts it: it said « Importer 3 tomes » and sent four.
    void a_file_dropped_alone_is_counted_with_the_folder()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_1");
            if (request.startsWith("POST /import "))
                return anOpening("Koro", {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s},
                                 {});
            return QByteArray();
        };
        const QString loose = m_folder.filePath(u"Tome 9.cbz"_s);
        aVolume(loose, 9);
        m_offers->offer({aCompleteSeries(), loose});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        QCOMPARE(m_review->importLabel(), Words::importVolumes(4));
    }

    /// A series the library holds whole beside a file that will go is not « Rien à
    /// importer »: « Fermer » there ran the queue and sent the file behind a button that
    /// said it closed.
    void a_file_that_will_go_is_something_to_import()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /preflight"))
                return aReservation("rcv_2");
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_h","root":"Blue Period","toSend":[],)"
                               R"("alreadyThere":)" + listed(komgaNames(1, 14))
                                   + R"(,"bytesToSend":0})");
        };
        const QString loose = m_folder.filePath(u"Tome 9.cbz"_s);
        aVolume(loose, 9);
        m_offers->offer({aKomgaSeries(), loose});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_review->view()->open();

        QVERIFY(!m_review->nothingToImport());
        QCOMPARE(m_review->importLabel(), Words::importVolumes(1));
        QVERIFY(m_review->importable());
    }

    /// Once its bytes are on their way, a card's sheet says it cannot be edited — the model
    /// refuses the correction anyway, and a field that took the keystrokes and dropped them
    /// would be the worse lie.
    void a_card_already_sending_shows_a_sheet_that_cannot_be_edited()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Koro", {u"Tome 1.cbz"_s}, {u"Tome 2.cbz"_s, u"Tome 3.cbz"_s});
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->open();
        QVERIFY(m_review->sheet().value(u"editable"_s).toBool());

        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QVERIFY(!m_review->sheet().value(u"editable"_s).toBool());
        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"title"_s, u"Autre"_s));
    }
    /// The frame rebuilds its tree, its list and a composition of every card on each change;
    /// a chunk landing (two thousand for a 7.9 GiB folder), a hash tick or the retry
    /// countdown changes nothing it shows, and must not cost that rebuild each time.
    void what_the_frame_does_not_read_does_not_rebuild_it()
    {
        aKomgaSeriesAnnounced();
        QSignalSpy rebuilt(m_review, &Review::changed);
        const QModelIndex row = m_imports->index(0);
        for (const Imports::Role role :
             {Imports::Role::Sent, Imports::Role::Checking, Imports::Role::RetryIn})
            emit m_imports->dataChanged(row, row, {int(role)});
        QCOMPARE(rebuilt.size(), 0);

        emit m_imports->dataChanged(row, row, {int(Imports::Role::Sent),
                                               int(Imports::Role::Stage_)});
        QCOMPARE(rebuilt.size(), 1);
        // No roles is every role.
        emit m_imports->dataChanged(row, row);
        QCOMPARE(rebuilt.size(), 2);
    }

    /// A series whose files the library all holds is still something to import when the
    /// declaration it carries differs from the library's: the button, the footer, the groups
    /// and the first sheet all say so — not « Rien à importer » over a `work.json` that will
    /// be written.
    void a_held_series_whose_declaration_differs_is_still_to_import()
    {
        aHeldSeriesAnnounced(R"("creates":[],"declarations":[{"path":"work.json","present":{},)"
                             R"("presentName":"Blue Period","differs":["title"]}])");
        m_imports->setDeclaring(0, u"work.json"_s, true);
        m_review->view()->select(u"all"_s);

        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 0);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 1);
        QVERIFY(!m_review->nothingToImport());
        // The reading direction nobody said is counted, and holds that `work.json` back: it
        // will be written, and a series read the wrong way round is not noticed.
        QVERIFY(!m_review->importable());
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' ' + Words::reviewFooter(1, 0));
        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QVERIFY(m_review->importable());
        QCOMPARE(m_review->importLabel(), Words::importVolumes(0));
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
        m_review->view()->open();
        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Blue Period"_s);
    }

    /// A container whose files are all there is identical only when nothing is created
    /// under it either: a series the library holds the files of but not the series is new.
    void a_held_series_that_would_be_created_is_not_identical()
    {
        aHeldSeriesAnnounced(R"("creates":[{"kind":"WORK","name":"Blue Period","at":""}])");
        m_review->view()->select(u"all"_s);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 0);
        QVERIFY(!m_review->nothingToImport());
    }

    void a_held_series_creating_and_declaring_nothing_is_identical()
    {
        aHeldSeriesAnnounced(R"("creates":[])");
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->select(u"all"_s);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 1);
        QVERIFY(m_review->nothingToImport());
    }

    /// Chapters are not edited from a line of text, so their field says it cannot be typed
    /// into — the sheet around it can.
    void a_field_that_cannot_be_edited_says_so_on_an_editable_sheet()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->select(idOf(0, komgaName(13)));

        QVERIFY(m_review->sheet().value(u"editable"_s).toBool());
        QVERIFY(fieldOnSheet(u"title"_s).value(u"editable"_s).toBool());
        QVERIFY(!fieldOnSheet(u"chapters"_s).value(u"editable"_s).toBool());
        QVERIFY(!m_review->edits()->edit(idOf(0, komgaName(13)), u"chapters"_s, u"Un, Deux"_s));
    }

    /// Each value is drawn as what it is: a summary as a paragraph, a date as a date, a number
    /// as a short numeric line, and the chapters as a list read line by line — all were the
    /// same one-line box, and the chapters came out as « , , , , , » for want of a `raw` in an
    /// object that has a title.
    void each_field_says_what_kind_of_value_it_is()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->select(idOf(0, komgaName(13)));

        QCOMPARE(fieldOnSheet(u"title"_s).value(u"kind"_s).toString(), u"line"_s);
        QCOMPARE(fieldOnSheet(u"summary"_s).value(u"kind"_s).toString(), u"long"_s);
        QCOMPARE(fieldOnSheet(u"publishedOn"_s).value(u"kind"_s).toString(), u"date"_s);
        QCOMPARE(fieldOnSheet(u"number"_s).value(u"kind"_s).toString(), u"number"_s);
        const QVariantMap chapters = fieldOnSheet(u"chapters"_s);
        QCOMPARE(chapters.value(u"kind"_s).toString(), u"chapters"_s);
        QVERIFY(chapters.value(u"text"_s).toString().isEmpty());
        // A chapter is its number, its title and its page: never a line of text with commas.
        for (const QVariant &line : chapters.value(u"lines"_s).toList()) {
            const QVariantMap chapter = line.toMap();
            QVERIFY(chapter.contains(u"number"_s) && chapter.contains(u"title"_s)
                    && chapter.contains(u"page"_s));
        }
    }

    /// Typing the number another volume holds is the drag's own move: the volume takes that
    /// place and the ones between slide by one, they are not swapped.
    void typing_a_number_another_volume_holds_moves_the_volume_there()
    {
        aKomgaSeriesAnnounced();
        const auto numberOf = [&](int tome) {
            m_review->view()->select(idOf(0, komgaName(tome)));
            return fieldOnSheet(u"number"_s).value(u"text"_s).toString();
        };
        const QString was13 = numberOf(13);
        const QString was14 = numberOf(14);
        QVERIFY(!was13.isEmpty() && !was14.isEmpty());
        // The first onto the place of the last: two volumes, so it is also a swap — the
        // one-step case the arrows make.
        QVERIFY(m_review->edits()->edit(idOf(0, komgaName(13)), u"number"_s, was14));
        QCOMPARE(numberOf(13), was14);
        QCOMPARE(numberOf(14), was13);
        QVERIFY(fieldOnSheet(u"number"_s).value(u"doubt"_s).toString().isEmpty());
    }

    /// A drag in the list says an order, and the order is the numbers: the two volumes swap
    /// the numbers they held between them, and the list reads them in that order. A set of
    /// numbers with a volume that has none is left alone.
    void dragging_a_volume_past_another_swaps_their_numbers()
    {
        aKomgaSeriesAnnounced();
        const QString first = idOf(0, komgaName(13));
        const QString second = idOf(0, komgaName(14));
        const auto numberOf = [&](const QString &id) {
            m_review->view()->select(id);
            return fieldOnSheet(u"number"_s).value(u"text"_s).toString();
        };
        const QString was13 = numberOf(first);
        const QString was14 = numberOf(second);
        QVERIFY(!was13.isEmpty() && !was14.isEmpty() && was13 != was14);

        QVERIFY(m_review->edits()->renumber({second, first}));
        QCOMPARE(numberOf(second), was13);
        QCOMPARE(numberOf(first), was14);
        // Said again in the order it already has, nothing is written.
        QVERIFY(!m_review->edits()->renumber({second, first}));
        // A line that is no volume refuses the whole.
        QVERIFY(!m_review->edits()->renumber({first, u"nowhere"_s}));
        QCOMPARE(numberOf(first), was14);
    }

    /// A choice takes one of its values: « droite » written into a `work.json` would be a
    /// reading direction the server does not read, so it changes nothing and says why.
    void a_choice_takes_only_one_of_its_values()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();

        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"readingDirection"_s, u"droite"_s));
        QCOMPARE(fieldOnCard(0, {}, u"readingDirection"_s).origin, Facts::Origin::Missing);
        QCOMPARE(fieldOnSheet(u"readingDirection"_s).value(u"doubt"_s).toString(),
                 Words::notAChoice(u"droite"_s));
        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"status"_s, u"fini"_s));
        QCOMPARE(fieldOnCard(0, {}, u"status"_s).origin, Facts::Origin::Missing);

        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"status"_s, u"completed"_s));
        QCOMPARE(fieldOnCard(0, {}, u"status"_s).value.toString(), u"completed"_s);
    }

    /// Filed, a card is read-only, sheet and fields alike.
    void a_filed_card_shows_a_sheet_nobody_can_type_into()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import/") && request.contains("/commit"))
                return aReply(200, R"({"root":"Koro","installed":3,"orphans":[],"open":false})");
            if (request.startsWith("POST /import"))
                return anOpening("Koro", {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s}, {});
            return aReply(200, R"({"path":"x","received":9})");
        };
        m_offers->offer({aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Filed));
        m_review->view()->open();

        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Koro Quest"_s);
        QVERIFY(!m_review->sheet().value(u"editable"_s).toBool());
        const QVariantList fields = m_review->sheet().value(u"fields"_s).toList();
        QVERIFY(!fields.isEmpty());
        QVERIFY(std::ranges::none_of(fields, [](const QVariant &one) {
            return one.toMap().value(u"editable"_s).toBool();
        }));
    }

    /// A folder of chapters lists its chapters and only them: the folder is never a line of
    /// its own list, nor one more in « Tout ».
    void a_folder_of_chapters_lists_its_chapters_and_not_itself()
    {
        const QString at = m_folder.filePath(u"Kagura"_s);
        QVERIFY(QDir().mkpath(at));
        aDeclaration(at, "Kagura");
        aVolume(at + u"/Chapitre 1.cbz"_s, 1);
        aVolume(at + u"/Chapitre 2.cbz"_s, 2);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Kagura", {u"Chapitre 1.cbz"_s, u"Chapitre 2.cbz"_s}, {});
            return QByteArray();
        };
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->open();

        QCOMPARE(lines().size(), 2);
        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 2);
        QVERIFY(std::ranges::none_of(lines(), [this](const QVariantMap &one) {
            return one.value(u"id"_s).toString() == idOf(0, {});
        }));
        QCOMPARE(lines().constFirst().value(u"name"_s).toString(), u"Chapitre 1"_s);
    }

    /// The list of a series of a thousand chapters reads each file of the walk a bounded
    /// number of times, and not once per line: weighing each file by walking every file
    /// read a million entries for one change, and the frame rebuilds on every change.
    /// A declared series of `chapters` chapters, the first half of which the library holds.
    void aLongSeriesAnnounced(int chapters)
    {
        const QString at = m_folder.filePath(u"Long"_s);
        QVERIFY(QDir().mkpath(at));
        aDeclaration(at, "Long");
        QStringList names;
        for (int number = 1; number <= chapters; ++number) {
            names << u"Chapitre %1.cbz"_s.arg(number);
            aVolume(at + u'/' + names.constLast(), number);
        }
        m_pretend->answerFor = [names, chapters](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Long", names.mid(chapters / 2), names.first(chapters / 2));
            return QByteArray();
        };
        m_offers->offer({at});
        QTRY_VERIFY_WITH_TIMEOUT(stageOf(0) == int(Imports::Stage::Ready), 60000);
    }

    void the_list_of_a_thousand_chapters_reads_each_file_a_few_times()
    {
        constexpr int Chapters = 1000;
        aLongSeriesAnnounced(Chapters);
        m_review->view()->select(idOf(0, {}));
        m_review->view()->toggleIdentical();

        const Review::Cost before = Review::cost();
        QCOMPARE(lines().size(), Chapters);
        const Review::Cost after = Review::cost();
        QCOMPARE(after.rows - before.rows, 1);
        QVERIFY2(after.entries - before.entries <= 2 * Chapters,
                 qPrintable(QString::number(after.entries - before.entries)));
    }

    /// Whether a container travels was asked by walking the files of its card: the whole
    /// drop's line of a thousand chapters walked five hundred of them for each question the
    /// list and its counters asked of it, four times over.
    void the_whole_drop_line_of_a_thousand_chapters_does_not_walk_its_files_per_chapter()
    {
        constexpr int Chapters = 1000;
        aLongSeriesAnnounced(Chapters);
        m_review->view()->select(u"all"_s);

        const qint64 before = Carrier::looked();
        QVERIFY(!m_review->rows().isEmpty());
        QCOMPARE(countOf(Review::Filter::Ready).value(u"count"_s).toInt(), 1);
        const qint64 looked = Carrier::looked() - before;
        QVERIFY2(looked <= 2 * Chapters, qPrintable(QString::number(looked)));
    }

    /// A universe of fourteen series of thirty volumes, half of which the library holds:
    /// each series' line asked whether it travels by walking all four hundred and twenty
    /// files again, and the list and its counters asked it of every series. Each line keeps
    /// the group its own files give it.
    void a_universe_of_fourteen_series_is_listed_without_walking_each_file_per_series()
    {
        constexpr int Series = 14;
        constexpr int Volumes = 30;
        const QString at = m_folder.filePath(u"Terres"_s);
        QVERIFY(QDir().mkpath(at));
        QFile declaration(at + u"/universe.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"name":"Terres"})");
        declaration.close();
        // The first seven held whole, the next four said whole, the last three saying
        // nothing of their own series.
        QStringList held;
        QStringList sent;
        for (int one = 0; one < Series; ++one) {
            const QString series = u"Série %1"_s.arg(one, 2, 10, QChar(u'0'));
            QVERIFY(QDir().mkpath(at + u'/' + series));
            if (one < 11)
                aDeclaration(at + u'/' + series, series.toUtf8());
            for (int number = 1; number <= Volumes; ++number) {
                const QString volume = series + u"/Tome %1.cbz"_s.arg(number);
                aVolume(at + u'/' + volume, number);
                (one < 7 ? held : sent) << volume;
            }
        }
        m_pretend->answerFor = [held, sent](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Terres", sent, held);
            return QByteArray();
        };
        m_offers->offer({at});
        QTRY_VERIFY_WITH_TIMEOUT(stageOf(0) == int(Imports::Stage::Reviewing), 60000);
        m_review->view()->select(idOf(0, {}));
        m_review->view()->toggleIdentical();

        const qint64 before = Carrier::looked();
        const QList<QVariantMap> listed = lines();
        const QVariantList counted = m_review->counts();
        const qint64 looked = Carrier::looked() - before;

        QCOMPARE(listed.size(), Series);
        for (const QVariantMap &line : listed) {
            const int one = line.value(u"name"_s).toString().right(2).toInt();
            const Review::Filter expected = one < 7 ? Review::Filter::Identical
                : one < 11                          ? Review::Filter::Ready
                                                    : Review::Filter::ToReview;
            QCOMPARE(line.value(u"group"_s).toInt(), int(expected));
        }
        QCOMPARE(counted.at(int(Review::Filter::Identical)).toMap().value(u"count"_s).toInt(), 7);
        QCOMPARE(counted.at(int(Review::Filter::Ready)).toMap().value(u"count"_s).toInt(), 4);
        QCOMPARE(counted.at(int(Review::Filter::ToReview)).toMap().value(u"count"_s).toInt(), 3);
        QVERIFY2(looked <= 2 * Series * Volumes, qPrintable(QString::number(looked)));
    }

    /// The footer and the caption under the tree each composed every card on every change,
    /// and the sheet of a suggested series composed its own card once more: fifty cards were
    /// a hundred and one compositions for three properties read once.
    void the_footer_the_caption_and_what_is_written_compose_each_card_once_per_change()
    {
        constexpr int Cards = 50;
        QStringList paths;
        for (int one = 0; one < Cards; ++one) {
            const QString at = m_folder.filePath(u"Série %1"_s.arg(one));
            QVERIFY(QDir().mkpath(at));
            for (int number = 1; number <= 2; ++number) {
                QFile file(at + u"/scan_%1.cbz"_s.arg(number));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(Zip::volume({}, "<ComicInfo><Series>Série " + QByteArray::number(one)
                                               + "</Series><Number>" + QByteArray::number(number)
                                               + "</Number></ComicInfo>"));
            }
            paths << at;
        }
        m_pretend->answerFor = sessions();
        m_offers->offer(paths);
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), Cards, 60000);
        m_review->view()->select(idOf(0, {}));
        emit m_imports->changed();

        const qint64 before = Review::cost().composes;
        const QString footer = m_review->footer();
        const QString note = m_review->railNote();
        const QVariantMap sheet = m_review->sheet();
        const qint64 composed = Review::cost().composes - before;

        QVERIFY(!footer.isEmpty());
        QCOMPARE(note, Words::comicInfoConverted(2 * Cards));
        QVERIFY(!sheet.value(u"writes"_s).toList().isEmpty());
        QVERIFY2(composed <= Cards, qPrintable(QString::number(composed)));
    }

    /// A series the library holds every file of, and that the server would create, still
    /// travels — its `work.json` is written — and is known to without walking its files.
    void a_series_whose_files_are_all_held_but_whose_work_is_created_still_travels()
    {
        aHeldSeriesAnnounced(R"("creates":[{"kind":"WORK","name":"Blue Period","at":""}])");
        const Card &card = m_imports->cards().at(0);
        const qint64 before = Carrier::looked();
        QVERIFY(Carrier::travels(card, card.road.sheets.constFirst()));
        const auto volume = std::ranges::find(card.road.sheets, komgaName(1), &Facts::Sheet::at);
        QVERIFY(volume != card.road.sheets.cend());
        QVERIFY(!Carrier::travels(card, *volume));
        QCOMPARE(Carrier::looked() - before, 0);
    }

    /// The line of the whole drop that names `card`, or nothing.
    QVariantMap lineOfCard(const Card &card) const
    {
        for (const QVariantMap &line : lines()) {
            if (line.value(u"id"_s).toString() == QString::number(card.token) + u'/')
                return line;
        }
        return {};
    }

    /// A server that opens one session per announcement, three volumes to send each, and
    /// refuses the announcement of the folder named `refused`.
    std::function<QByteArray(const QByteArray &)> refusing(const QByteArray &refused)
    {
        return [this, refused](const QByteArray &request) {
            if (request.startsWith("POST /import ")) {
                if (request.contains("\"root\":\"" + refused + '"'))
                    return aReply(400, R"({"error":"refused"})");
                const QByteArray id = "imp_r" + QByteArray::number(++m_opened);
                return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],)"
                                   R"("toSend":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("alreadyThere":[],"bytesToSend":27})");
            }
            if (request.contains("/commit"))
                return aReply(200, R"({"root":"x","installed":3,"orphans":[],"open":false})");
            if (request.startsWith("PUT "))
                return aReply(200, R"({"path":"x","received":9})");
            return aReply(204, {});
        };
    }

    /// One series refused among fifty said nothing in the frame, which drew it as ready to
    /// go: its reason is said on its line, it is counted to review, and it holds back none
    /// of the forty-nine others, whose volumes « Importer » counts and sends.
    void one_failure_among_fifty_is_said_on_its_line_and_holds_back_nobody()
    {
        constexpr int Series = 50;
        QStringList paths;
        for (int one = 0; one < Series; ++one)
            paths << aCompleteSeries(u"Lot %1"_s.arg(one));
        m_pretend->answerFor = refusing("Lot 17");
        m_offers->offer(paths);
        const int refused = rowNamed(u"Lot 17"_s);
        QTRY_COMPARE_WITH_TIMEOUT(stageOf(refused), int(Imports::Stage::Failed), 60000);
        for (int row = 0; row < Series; ++row) {
            if (row != refused)
                QTRY_COMPARE_WITH_TIMEOUT(stageOf(row), int(Imports::Stage::Ready), 60000);
        }
        m_review->view()->select(u"all"_s);

        const Card &card = m_imports->cards().at(refused);
        QVERIFY(!card.trouble.isEmpty());
        const QVariantMap line = lineOfCard(card);
        QCOMPARE(line.value(u"chip"_s).toString(), Words::failedChip());
        QCOMPARE(line.value(u"detail"_s).toString(), card.trouble);
        QCOMPARE(line.value(u"group"_s).toInt(), int(Review::Filter::ToReview));
        QCOMPARE(line.value(u"origin"_s).toInt(), int(Review::Origin::Missing));
        QCOMPARE(line.value(u"checkable"_s).toBool(), false);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 1);
        QCOMPARE(m_review->importLabel(), Words::importVolumes(3 * (Series - 1)));

        m_pretend->heard.clear();
        m_queue->confirm();
        QTRY_COMPARE_WITH_TIMEOUT(m_pretend->heard.count("/commit"), Series - 1, 60000);
        QCOMPARE(stageOf(refused), int(Imports::Stage::Failed));
    }

    /// The second of two folders of one name is refused before anything is read, and the
    /// frame showed only the first: both are listed, and the second says why it stays.
    void two_folders_of_one_name_both_show_and_the_second_says_why()
    {
        m_pretend->answerFor = sessions();
        const QString rayon = m_folder.filePath(u"Rayon"_s);
        for (const QString &under : {u"Death Note"_s, u"Vieux/Death Note"_s}) {
            const QString at = rayon + u'/' + under;
            QVERIFY(QDir().mkpath(at));
            aDeclaration(at, "Death Note");
            aVolume(at + u"/Tome 1.cbz"_s, 1);
        }
        m_offers->offer({rayon});
        QCOMPARE(m_imports->rowCount({}), 2);
        const int refused = stageOf(0) == int(Imports::Stage::Failed) ? 0 : 1;
        QTRY_VERIFY(stageOf(1 - refused) == int(Imports::Stage::Ready));
        m_review->view()->select(u"all"_s);

        QCOMPARE(lines().size(), 2);
        const QVariantMap line = lineOfCard(m_imports->cards().at(refused));
        QCOMPARE(line.value(u"detail"_s).toString(),
                 Words::sameDestination(u"Vieux/Death Note"_s, u"Death Note"_s));
        QCOMPARE(line.value(u"name"_s).toString(), u"Death Note"_s);
        QCOMPARE(line.value(u"chip"_s).toString(), Words::failedChip());
    }

    /// A card refused has nothing that travels, and the frame read that as « Identique »
    /// for a series the library already held — or « Prête » for one whose announcement was
    /// refused: neither goes anywhere, and both are to review.
    void a_failed_card_is_never_ready_nor_identical()
    {
        // Held whole, then corrected: announced again, its old session cannot be dropped.
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(500, R"({"error":"internal error"})");
            if (request.startsWith("POST /import ")
                && request.contains("\"root\":\"Blue Period\""))
                return aReply(200, R"({"id":"imp_h","root":"Blue Period","toSend":[],)"
                                   R"("alreadyThere":)" + listed(komgaNames(1, 14))
                                       + R"(,"bytesToSend":0,"creates":[]})");
            return aReply(400, R"({"error":"refused"})");
        };
        m_offers->offer({aKomgaSeries(), aCompleteSeries()});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Failed));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->select(u"all"_s);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 1);

        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"title"_s, u"Bleu"_s));
        m_queue->confirm();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Failed));
        QVERIFY(!m_imports->cards().at(0).road.alreadyThere.isEmpty());

        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 0);
        QCOMPARE(countOf(Review::Filter::Ready).value(u"count"_s).toInt(), 0);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 2);
        // And neither is something to import: the frame can still be closed.
        QVERIFY(m_review->nothingToImport());
    }

    /// A failed card could only be left in the frame: its sheet offers to take it away,
    /// which abandons it, and the sheet goes back to the whole drop.
    void removing_a_failed_card_takes_its_line_away()
    {
        m_pretend->answerFor = refusing("Mira");
        m_offers->offer({aCompleteSeries(), aCompleteSeries(u"Mira"_s)});
        const int refused = rowNamed(u"Mira"_s);
        QTRY_COMPARE(stageOf(refused), int(Imports::Stage::Failed));
        QTRY_COMPARE(stageOf(1 - refused), int(Imports::Stage::Ready));
        const QString id = idOf(refused, {});
        const QString kept = idOf(1 - refused, {});
        m_review->view()->select(id);
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"removable"_s).toBool(), true);
        QCOMPARE(sheet.value(u"editable"_s).toBool(), false);
        const QVariantList notes = sheet.value(u"notes"_s).toList();
        QVERIFY(!notes.isEmpty());
        QCOMPARE(notes.constFirst().toMap().value(u"text"_s).toString(),
                 m_imports->cards().at(refused).trouble);
        QCOMPARE(notes.constFirst().toMap().value(u"alarming"_s).toBool(), true);
        // A card that can still go is not removed from here.
        QVERIFY(!m_review->edits()->remove(kept));
        m_review->view()->select(kept);
        QVERIFY(m_review->sheet().contains(u"removable"_s));
        QCOMPARE(m_review->sheet().value(u"removable"_s).toBool(), false);
        m_review->view()->select(id);

        QVERIFY(m_review->edits()->remove(id));
        QCOMPARE(m_imports->rowCount({}), 1);
        QCOMPARE(lines().size(), 1);
        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), Words::wholeApport());
    }

    /// Opened on the first thing to review, the frame opened on a series that only needed a
    /// word, and the one refused waited below, where nothing said it would never go.
    void the_frame_opens_on_a_failure_first()
    {
        m_pretend->answerFor = refusing("Koro");
        m_offers->offer({aKomgaSeries(), aCompleteSeries()});
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Failed));
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        m_review->view()->open();
        QCOMPARE(m_review->sheet().value(u"id"_s).toString(), idOf(1, {}));
    }

    /// A series declared whole, credited to `authors`.
    QString aCreditedSeries(const QString &name, const QStringList &authors)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QFile declaration(at + u"/work.json"_s);
        [&] { QVERIFY(declaration.open(QIODevice::WriteOnly)); }();
        declaration.write(QJsonDocument(QJsonObject{
                                            {u"leaf"_s, 1},
                                            {u"title"_s, name},
                                            {u"readingDirection"_s, u"RIGHT_TO_LEFT"_s},
                                            {u"authors"_s, QJsonArray::fromStringList(authors)},
                                            {u"artists"_s, QJsonArray::fromStringList(authors)},
                                        })
                              .toJson(QJsonDocument::Compact));
        declaration.close();
        aVolume(at + u"/Tome 1.cbz"_s, 1);
        return at;
    }

    /// How many cards are ready to go.
    int readyCards() const
    {
        return int(std::ranges::count(m_imports->cards(), Imports::Stage::Ready, &Card::stage));
    }

    /// The names of the lines shown, in their order.
    QStringList namesShown() const
    {
        QStringList out;
        for (const QVariantMap &line : lines())
            out << line.value(u"name"_s).toString();
        return out;
    }

    /// Fifty series in a list are found by typing, not by scrolling: a word of the name or
    /// of an author keeps the lines holding it, whatever its case and its accents, and the
    /// counters count what is left. « Simón » answers « mon » as « Monster » does.
    void a_search_keeps_the_series_whose_name_or_author_holds_it_whatever_the_accents()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aCreditedSeries(u"Monster"_s, {u"Naoki Urasawa"_s}),
                          aCreditedSeries(u"Bleach"_s, {u"Simón"_s}),
                          aCreditedSeries(u"Naruto"_s, {u"Masashi Kishimoto"_s})});
        QTRY_COMPARE(readyCards(), 3);
        m_review->view()->select(u"all"_s);
        QCOMPARE(lines().size(), 3);

        QSignalSpy changed(m_review, &Review::changed);
        m_review->view()->setSearch(u"mon"_s);
        QCOMPARE(changed.size(), 1);
        QStringList found = namesShown();
        found.sort();
        QCOMPARE(found, (QStringList{u"Bleach"_s, u"Monster"_s}));
        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 2);
        QCOMPARE(countOf(Review::Filter::Ready).value(u"count"_s).toInt(), 2);
        // The same word folded is no new search, and the frame is not rebuilt for it.
        m_review->view()->setSearch(u"MON"_s);
        QCOMPARE(changed.size(), 1);

        m_review->view()->setSearch(u"MONSTER"_s);
        QCOMPARE(namesShown(), QStringList{u"Monster"_s});
        QCOMPARE(m_review->view()->search(), u"MONSTER"_s);
        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 1);
        // Searching wrote nothing on any card.
        for (const Card &card : m_imports->cards())
            QVERIFY(!card.road.edited);
    }

    /// A universe is a line of the whole drop, its series are not: one of them answering
    /// keeps the universe's line, or the series searched for could not be reached.
    void a_universe_line_stays_when_one_of_its_series_answers()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverse(), aCompleteSeries()});
        QTRY_COMPARE(m_imports->cards().size(), 2);
        QTRY_VERIFY(std::ranges::all_of(m_imports->cards(), [](const Card &card) {
            return card.stage == Imports::Stage::Ready || card.stage == Imports::Stage::Reviewing;
        }));
        m_review->view()->select(u"all"_s);

        m_review->view()->setSearch(u"mages"_s);
        QCOMPARE(namesShown(), QStringList{u"Terres d’Arran"_s});
        m_review->view()->setSearch(u"koro"_s);
        QCOMPARE(namesShown(), QStringList{u"Koro Quest"_s});
    }

    /// The tree is how the drop is found again: a search that left nothing in the list
    /// leaves every node of the tree where it was, and no group says it holds nothing.
    void a_search_leaves_the_tree_whole()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverse(), aCompleteSeries()});
        QTRY_COMPARE(m_imports->cards().size(), 2);
        QTRY_VERIFY(std::ranges::all_of(m_imports->cards(), [](const Card &card) {
            return card.stage == Imports::Stage::Ready || card.stage == Imports::Stage::Reviewing;
        }));
        m_review->view()->select(u"all"_s);
        const QStringList tree = railIds();

        m_review->view()->setSearch(u"introuvable"_s);
        QCOMPARE(railIds(), tree);
        QVERIFY(m_review->rows().isEmpty());
        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 0);
    }

    /// The frame opened again is opened on what is to review: a search left from before
    /// hid it.
    void opening_the_frame_again_forgets_the_search()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aCompleteSeries(), aCompleteSeries(u"Mira"_s)});
        QTRY_COMPARE(readyCards(), 2);
        m_review->view()->select(u"all"_s);
        m_review->view()->setSearch(u"introuvable"_s);
        QVERIFY(lines().isEmpty());

        m_review->view()->open();
        QCOMPARE(m_review->view()->search(), QString());
        m_review->view()->select(u"all"_s);
        QCOMPARE(lines().size(), 2);
    }

    /// The identical group is folded, and the whole drop said nothing of what it spared:
    /// fifteen series the library already holds are said once, with their weight.
    void the_whole_drop_says_what_its_identical_series_spare()
    {
        constexpr int Series = 15;
        QStringList paths;
        for (int one = 0; one < Series; ++one)
            paths << aCompleteSeries(u"Lot %1"_s.arg(one));
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return aReply(200, R"({"id":"imp_held","root":"x","creates":[],"toSend":[],)"
                                   R"("alreadyThere":["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"],)"
                                   R"("bytesToSend":0})");
            return aReply(204, {});
        };
        m_offers->offer(paths);
        QTRY_COMPARE(readyCards(), Series);
        qint64 bytes = 0;
        for (const Card &card : m_imports->cards()) {
            for (const Manifest::Entry &file : card.road.tree.files)
                bytes += file.size;
        }

        m_review->view()->select(u"all"_s);
        QCOMPARE(m_review->note(), Words::identicalSpared(Series, bytes));
        // A series' own note is the one it always said.
        m_review->view()->select(idOf(0, {}));
        QCOMPARE(m_review->note(), Words::alreadyHeld(u"Koro Quest"_s, 3, 3));
    }

    /// A search over fifty cards reads each of their sheets a few times, not once per
    /// line: the list is rebuilt on every key typed.
    void the_search_reads_each_sheet_a_few_times()
    {
        constexpr int Cards = 50;
        QStringList paths;
        for (int one = 0; one < Cards; ++one)
            paths << aCreditedSeries(u"Série %1"_s.arg(one), {u"Auteur %1"_s.arg(one)});
        m_pretend->answerFor = sessions();
        m_offers->offer(paths);
        QTRY_COMPARE_WITH_TIMEOUT(readyCards(), Cards, 60000);
        qint64 entries = 0;
        qint64 sheets = 0;
        for (const Card &card : m_imports->cards()) {
            entries += card.road.tree.files.size();
            sheets += card.road.sheets.size();
        }
        m_review->view()->select(u"all"_s);
        emit m_imports->changed();

        const Review::Cost before = Review::cost();
        m_review->view()->setSearch(u"a"_s);
        const QVariantList rows = m_review->rows();
        const Review::Cost after = Review::cost();
        QVERIFY(!rows.isEmpty());
        QVERIFY2(after.entries - before.entries <= entries + sheets,
                 qPrintable(u"%1 entries for %2 files and %3 sheets"_s
                                .arg(after.entries - before.entries)
                                .arg(entries)
                                .arg(sheets)));
    }

    /// A series as a Komga library leaves it, under its own name: a ComicInfo saying the
    /// series and the number, and nothing of the direction it reads in.
    QString aKomgaSeriesNamed(const QString &name, int volumes = 2)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        for (int number = 1; number <= volumes; ++number) {
            QFile file(at + u"/scan_%1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}, "<ComicInfo><Series>" + name.toUtf8() + "</Series><Number>"
                                           + QByteArray::number(number)
                                           + "</Number><Writer>Yuki</Writer>"
                                             "<Penciller>Yuki</Penciller></ComicInfo>"));
        }
        return at;
    }

    /// A series that stops the checking step and holds nothing back: who wrote and drew it
    /// and which way it reads are said by its ComicInfo, and its numbers are not — guessed
    /// from the names of its files.
    QString aGuessedSeriesNamed(const QString &name, int volumes = 2)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        for (int number = 1; number <= volumes; ++number) {
            QFile file(at + u"/Tome %1.cbz"_s.arg(number));
            [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
            file.write(Zip::volume({}, "<ComicInfo><Series>" + name.toUtf8()
                                           + "</Series><Writer>Yuki</Writer><Penciller>Yuki"
                                             "</Penciller><Manga>YesAndRightToLeft</Manga>"
                                             "</ComicInfo>"));
        }
        return at;
    }

    /// `count` Komga series announced to a server holding nothing, each stopped on its
    /// direction, listed in the whole drop.
    void komgaSeriesListed(int count)
    {
        QStringList paths;
        for (int one = 0; one < count; ++one)
            paths << aKomgaSeriesNamed(u"Komga %1"_s.arg(one));
        m_pretend->answerFor = sessions();
        m_offers->offer(paths);
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), count, 60000);
        m_review->view()->select(u"all"_s);
    }

    const Facts::Field &rootField(int row, const QString &key) const
    {
        const Card &card = m_imports->cards().at(row);
        const Facts::Sheet &sheet = card.road.sheets.constFirst();
        return *std::ranges::find(sheet.fields, key, &Facts::Field::key);
    }

    /// Maj and an arrow tick every line passed, in the order the list draws them, and the
    /// frame is built again once for the whole run rather than once per line.
    void a_range_ticks_every_line_between_in_the_order_drawn_and_says_it_once()
    {
        komgaSeriesListed(5);
        const QList<QVariantMap> shown = lines();
        QCOMPARE(shown.size(), 5);
        const QString from = shown.at(3).value(u"id"_s).toString();
        const QString to = shown.at(1).value(u"id"_s).toString();

        QSignalSpy changed(m_review, &Review::changed);
        QCOMPARE(m_review->ticks()->tickRange(from, to), 3);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(m_review->ticks()->ticked(), 3);
        const QList<QVariantMap> after = lines();
        for (int at = 0; at < after.size(); ++at)
            QCOMPARE(after.at(at).value(u"checked"_s).toBool(), at >= 1 && at <= 3);
        QVERIFY(m_review->ticks()->groupedShown());
        // Nothing new to tick: nothing said.
        QCOMPARE(m_review->ticks()->tickRange(to, from), 0);
        QCOMPARE(changed.size(), 1);
    }

    /// « Tout » under « À revoir » ticks the lines that filter shows, and then says all of
    /// them are: it said « some », for the ready series the filter hides.
    void every_line_ticked_under_a_filter_is_said_all_ticked()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aKomgaSeriesNamed(u"Komga A"_s), aKomgaSeriesNamed(u"Komga B"_s),
                          aCompleteSeries(u"Prête C"_s)});
        QTRY_COMPARE(m_imports->reviewing(), 2);
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->select(u"all"_s);
        m_review->view()->setFilter(int(Review::Filter::ToReview));

        m_review->ticks()->tickShown(true);
        QCOMPARE(m_review->ticks()->ticked(), 2);
        QCOMPARE(m_review->ticks()->tickState(), 2);
    }

    /// Opening the frame on fifty series walks each series a bounded number of times: the
    /// tree it opens on, the first to review in it, the nodes folded round it.
    void opening_fifty_series_costs_a_bounded_amount_of_work()
    {
        constexpr int Series = 50;
        komgaSeriesListed(Series);
        emit m_imports->changed();

        const Review::Cost before = Review::cost();
        m_review->view()->open();
        const Review::Cost after = Review::cost();
        QVERIFY(!cardShown().isEmpty());
        const qint64 looks = after.looks - before.looks;
        const qint64 entries = after.entries - before.entries;
        const qint64 proposals = after.proposals - before.proposals;
        const qint64 composes = after.composes - before.composes;
        const QString spent = u"%1 looks, %2 entries, %3 proposals, %4 compositions"_s
                                  .arg(looks).arg(entries).arg(proposals).arg(composes);
        // Each series a handful of times, never once per other series.
        QVERIFY2(looks <= 6 * Series && entries <= 4 * Series && proposals <= 2
                     && composes <= Series,
                 qPrintable(spent));
    }

    /// Four series nobody said the direction of were four sheets to open and four boxes to
    /// fill: ticked, they take it in one gesture, each in its own `work.json`, carried by the
    /// announcement « Importer » makes again for each.
    void ticking_four_series_and_applying_a_direction_writes_it_in_each()
    {
        komgaSeriesListed(4);
        for (int row = 0; row < 4; ++row)
            m_review->ticks()->tick(idOf(row, {}), true);
        QCOMPARE(m_review->ticks()->ticked(), 4);
        QCOMPARE(m_review->ticks()->tickedLabel(), Words::tickedCount(4));
        QVERIFY(m_review->ticks()->groupedShown());

        QCOMPARE(m_review->ticks()->applyToTicked({{u"readingDirection"_s, u"RIGHT_TO_LEFT"_s}}), 4);
        QCOMPARE(m_review->ticks()->grouped().value(u"outcome"_s).toString(),
                 Words::groupedOutcome(4, 0, 0));
        for (int row = 0; row < 4; ++row) {
            const Facts::Field &direction = rootField(row, u"readingDirection"_s);
            QCOMPARE(direction.value.toString(), u"RIGHT_TO_LEFT"_s);
            QCOMPARE(direction.origin, Facts::Origin::Said);
            QVERIFY(direction.edited);
        }
        // Given again, nothing changes and the frame says why.
        QCOMPARE(m_review->ticks()->applyToTicked({{u"readingDirection"_s, u"RIGHT_TO_LEFT"_s}}), 0);
        QCOMPARE(m_review->ticks()->grouped().value(u"outcome"_s).toString(),
                 Words::groupedOutcome(0, 4, 0));

        m_pretend->heard.clear();
        m_queue->confirm();
        QTRY_COMPARE(m_pretend->heard.count("DELETE /import/"), 4);
        QTRY_COMPARE(m_pretend->heard.count("POST /import "), 4);
        QVERIFY2(m_pretend->heard.count(R"(readingDirection\":\"RIGHT_TO_LEFT)") >= 4,
                 m_pretend->heard.constData());
    }

    /// A series in two declared editions lists the two editions, and says how many they are
    /// before how many volumes they hold: both editions' volumes put end to end were seventy-six
    /// lines of one long series, and « 76 tomes » read as a single edition.
    void a_series_lists_its_editions_and_not_their_volumes()
    {
        const QString at = m_folder.filePath(u"Dragon"_s);
        QVERIFY(QDir().mkpath(at));
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"title":"Dragon"})");
        declaration.close();
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            QVERIFY(QDir().mkpath(at + u'/' + edition));
            QFile said(at + u'/' + edition + u"/edition.json"_s);
            QVERIFY(said.open(QIODevice::WriteOnly));
            said.write(R"({"leaf":1,"name":")" + edition.toUtf8() + R"("})");
            said.close();
            aVolume(at + u'/' + edition + u"/Tome 1.cbz"_s, 1);
            aVolume(at + u'/' + edition + u"/Tome 2.cbz"_s, 2);
        }
        m_pretend->answerFor = sessions();
        m_offers->offer({at});
        QTRY_COMPARE(m_imports->reviewing(), 1);
        m_review->view()->select(idOf(0, {}));

        QStringList names;
        for (const QVariantMap &one : lines())
            names << one.value(u"name"_s).toString();
        QCOMPARE(names.size(), 2);
        QVERIFY(names.contains(u"Perfect"_s));
        QVERIFY(names.contains(u"Original"_s));
        m_review->view()->select(u"all"_s);
        const QString detail = lineNamed(u"Dragon"_s).value(u"detail"_s).toString();
        QVERIFY2(detail.startsWith(u"2 éditions"_s), qPrintable(detail));
        QVERIFY2(detail.contains(u"4 tomes"_s), qPrintable(detail));
    }

    /// A series in declared editions reads in each edition's direction: the value goes into
    /// every `edition.json` under the series ticked, not into a field the series has none of.
    void a_series_with_declared_editions_receives_the_value_in_each_edition()
    {
        const QString at = m_folder.filePath(u"Dragon"_s);
        QVERIFY(QDir().mkpath(at));
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"title":"Dragon"})");
        declaration.close();
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            QVERIFY(QDir().mkpath(at + u'/' + edition));
            QFile said(at + u'/' + edition + u"/edition.json"_s);
            QVERIFY(said.open(QIODevice::WriteOnly));
            said.write(R"({"leaf":1,"name":")" + edition.toUtf8() + R"("})");
            said.close();
            aVolume(at + u'/' + edition + u"/Tome 1.cbz"_s, 1);
        }
        m_pretend->answerFor = sessions();
        m_offers->offer({at});
        QTRY_COMPARE(m_imports->reviewing(), 1);
        m_review->view()->select(u"all"_s);
        m_review->ticks()->tick(idOf(0, {}), true);

        QCOMPARE(m_review->ticks()->applyToTicked({{u"readingDirection"_s, u"LEFT_TO_RIGHT"_s}}), 1);
        const Card &card = m_imports->cards().at(0);
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            const auto sheet = std::ranges::find(card.road.sheets, edition, &Facts::Sheet::at);
            QVERIFY(sheet != card.road.sheets.cend());
            const auto direction =
                std::ranges::find(sheet->fields, u"readingDirection"_s, &Facts::Field::key);
            QVERIFY(direction != sheet->fields.cend());
            QCOMPARE(direction->value.toString(), u"LEFT_TO_RIGHT"_s);
            QVERIFY(direction->edited);
        }
    }

    /// Applied to twenty-four series one by one, a value said the queue changed twenty-four
    /// times and rebuilt the frame each time: in one batch, the frame hears of it once or
    /// twice, and walks the sheets of the series ticked and no others.
    void applying_to_twenty_four_series_rebuilds_the_frame_once()
    {
        constexpr int Series = 24;
        komgaSeriesListed(Series);
        m_review->ticks()->tickShown(true);
        QCOMPARE(m_review->ticks()->ticked(), Series);
        qint64 sheets = 0;
        for (const Card &card : m_imports->cards())
            sheets += card.road.sheets.size();
        QSignalSpy changed(m_review, &Review::changed);

        const Review::Cost before = Review::cost();
        QCOMPARE(m_review->ticks()->applyToTicked({{u"readingDirection"_s, u"RIGHT_TO_LEFT"_s},
                                          {u"status"_s, QVariant()}}),
                 Series);
        QVERIFY(!m_review->rows().isEmpty());
        const Review::Cost after = Review::cost();

        QCOMPARE(after.rows - before.rows, 1);
        QCOMPARE(after.composes - before.composes, 0);
        QVERIFY2(changed.size() <= 2, qPrintable(QString::number(changed.size())));
        QVERIFY2(after.grouped - before.grouped <= sheets,
                 qPrintable(u"%1 for %2 sheets"_s.arg(after.grouped - before.grouped).arg(sheets)));
        // A key given no value changes nothing.
        for (int row = 0; row < Series; ++row)
            QVERIFY(!rootField(row, u"status"_s).edited);
    }

    /// A tick acts only on what is seen: changing the filter forgets the ticks, « Tout »
    /// under a search ticks only the lines it shows, and a line identical, refused or
    /// already gone has no box.
    void a_tick_never_acts_out_of_sight()
    {
        QStringList paths{aKomgaSeriesNamed(u"Komga A"_s), aKomgaSeriesNamed(u"Komga B"_s),
                          aCompleteSeries(u"Gardée"_s), aCompleteSeries(u"Refusée"_s),
                          aCompleteSeries(u"Partie"_s)};
        m_pretend->answerFor = [this](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();  // nothing that goes is ever answered: it stays in flight
            const QByteArray id = "imp_t" + QByteArray::number(++m_opened);
            if (request.contains("\"root\":\"Refus"))
                return aReply(400, R"({"error":"refused"})");
            const bool held = request.contains("\"root\":\"Gard");
            const QByteArray three = R"(["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"])";
            return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],"toSend":)"
                                   + (held ? QByteArray("[]") : three) + R"(,"alreadyThere":)"
                                   + (held ? three : QByteArray("[]")) + R"(,"bytesToSend":9})");
        };
        m_offers->offer(paths);
        QTRY_COMPARE(stageOf(rowNamed(u"Refusée"_s)), int(Imports::Stage::Failed));
        QTRY_COMPARE(m_imports->reviewing(), 2);
        QTRY_COMPARE(stageOf(rowNamed(u"Partie"_s)), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(rowNamed(u"Gardée"_s)), int(Imports::Stage::Ready));
        m_review->view()->select(u"all"_s);
        m_review->view()->toggleIdentical();
        // Identical, before « Importer » lets it go with nothing to send.
        QVERIFY(!lineOfCard(m_imports->cards().at(rowNamed(u"Gardée"_s)))
                     .value(u"checkable"_s)
                     .toBool());
        m_review->ticks()->tick(idNamed(u"Gardée"_s), true);
        QCOMPARE(m_review->ticks()->ticked(), 0);
        m_queue->send();
        QTRY_COMPARE(stageOf(rowNamed(u"Partie"_s)), int(Imports::Stage::Sending));

        for (const QString &name : {u"Refusée"_s, u"Partie"_s}) {
            const QVariantMap line = lineOfCard(m_imports->cards().at(rowNamed(name)));
            QVERIFY2(!line.isEmpty(), qPrintable(name));
            QVERIFY2(!line.value(u"checkable"_s).toBool(), qPrintable(name));
            m_review->ticks()->tick(idNamed(name), true);
        }
        QCOMPARE(m_review->ticks()->ticked(), 0);

        m_review->ticks()->tick(idNamed(u"Komga A"_s), true);
        m_review->ticks()->tick(idNamed(u"Komga B"_s), true);
        QCOMPARE(m_review->ticks()->ticked(), 2);
        QCOMPARE(m_review->ticks()->tickState(), 2);
        QVERIFY(lineOfCard(m_imports->cards().at(rowNamed(u"Komga A"_s)))
                    .value(u"checked"_s)
                    .toBool());
        m_review->view()->setFilter(int(Review::Filter::ToReview));
        QCOMPARE(m_review->ticks()->ticked(), 0);
        QCOMPARE(m_review->ticks()->tickState(), 0);

        m_review->view()->setFilter(int(Review::Filter::All));
        m_review->view()->setSearch(u"komga a"_s);
        m_review->ticks()->tickShown(true);
        QCOMPARE(m_review->ticks()->ticked(), 1);
        m_review->view()->setSearch(QString());
        QCOMPARE(m_review->ticks()->ticked(), 0);
        m_review->ticks()->tick(idNamed(u"Komga A"_s), true);
        QCOMPARE(m_review->ticks()->tickState(), 1);
        m_review->ticks()->tickShown(false);
        QCOMPARE(m_review->ticks()->ticked(), 0);
    }

    /// Only what is copied from one series to the next is offered for all of them: a title
    /// or a number given to twenty series at once is twenty series of one name.
    void names_and_numbers_are_never_offered_together()
    {
        komgaSeriesListed(2);
        m_review->ticks()->tick(idOf(0, {}), true);
        const QVariantMap grouped = m_review->ticks()->grouped();
        QStringList keys;
        for (const QVariant &one : grouped.value(u"fields"_s).toList())
            keys << one.toMap().value(u"key"_s).toString();
        QCOMPARE(keys, (QStringList{u"readingDirection"_s, u"language"_s, u"status"_s}));
        QCOMPARE(grouped.value(u"title"_s).toString(), Words::forTicked(1));
        QCOMPARE(grouped.value(u"aside"_s).toString(), Words::groupedActions());
        QCOMPARE(grouped.value(u"applyLabel"_s).toString(), Words::applyToTicked(1));
        QCOMPARE(grouped.value(u"note"_s).toString(), Words::groupedNote());
        for (const QVariant &one : grouped.value(u"fields"_s).toList()) {
            const QVariantMap first = one.toMap().value(u"choices"_s).toList().constFirst().toMap();
            QVERIFY(first.value(u"value"_s).isNull());
            QCOMPARE(first.value(u"label"_s).toString(), Words::leaveUnchanged());
        }
        // Applying a title is no gesture this offers: nothing is written.
        QCOMPARE(m_review->ticks()->applyToTicked({{u"title"_s, u"Même nom"_s}}), 0);
        QVERIFY(!m_imports->cards().at(0).road.edited);
    }

    /// The languages offered are those the drop already says, then the three most read:
    /// offered every tag there is, the list was a wall; offered only the three, a series in
    /// German could not be ticked into German.
    void the_languages_offered_are_those_of_the_drop_and_three_more()
    {
        const QString at = m_folder.filePath(u"Zauber"_s);
        QVERIFY(QDir().mkpath(at));
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"title":"Zauber","language":"de"})");
        declaration.close();
        aVolume(at + u"/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = sessions();
        m_offers->offer({at, aKomgaSeriesNamed(u"Komga"_s)});
        QTRY_COMPARE(m_imports->reviewing(), 2);
        m_review->view()->select(u"all"_s);
        m_review->ticks()->tick(idOf(0, {}), true);

        QVariantList choices;
        for (const QVariant &one : m_review->ticks()->grouped().value(u"fields"_s).toList()) {
            if (one.toMap().value(u"key"_s).toString() == u"language"_s)
                choices = one.toMap().value(u"choices"_s).toList();
        }
        QStringList values;
        for (const QVariant &one : choices.mid(1))
            values << one.toMap().value(u"value"_s).toString();
        QCOMPARE(values, (QStringList{u"de"_s, u"fr"_s, u"en"_s, u"ja"_s}));
        QCOMPARE(choices.at(1).toMap().value(u"label"_s).toString(), Words::language(u"de"_s));
    }

    /// The id of the card at the root of the sheet shown.
    QString cardShown() const
    {
        return m_review->sheet().value(u"id"_s).toString().section(u'/', 0, 0) + u'/';
    }

    /// The rail's line `id`, or nothing.
    QVariantMap railLine(const QString &id) const
    {
        for (const QVariant &one : m_review->rail()) {
            if (one.toMap().value(u"id"_s).toString() == id)
                return one.toMap();
        }
        return {};
    }

    /// Five series, three of them stopped: « Revoir la suivante » goes through the three in
    /// the order of the tree, one each, and then comes back to the first — rather than a
    /// reader hunting fifty lines for the next one that still asks something.
    void reviewing_the_next_walks_every_series_to_review_once_then_comes_back()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aKomgaSeriesNamed(u"Komga A"_s), aCompleteSeries(u"Prête B"_s),
                          aKomgaSeriesNamed(u"Komga C"_s), aCompleteSeries(u"Prête D"_s),
                          aKomgaSeriesNamed(u"Komga E"_s)});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        QTRY_COMPARE(readyCards(), 2);
        m_review->view()->select(u"all"_s);
        QCOMPARE(m_review->toReview(), 3);

        QStringList walked;
        for (int step = 0; step < 4; ++step) {
            QVERIFY(m_review->view()->reviewNext());
            walked << cardShown();
        }
        QCOMPARE(walked, (QStringList{idNamed(u"Komga A"_s), idNamed(u"Komga C"_s),
                                      idNamed(u"Komga E"_s), idNamed(u"Komga A"_s)}));
    }

    /// What is ready or identical asks nothing, and the walk does not stop there.
    void the_next_one_skips_what_is_ready_and_what_is_identical()
    {
        m_pretend->answerFor = [this](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return aReply(204, {});
            const QByteArray id = "imp_n" + QByteArray::number(++m_opened);
            const QByteArray three = R"(["Tome 1.cbz","Tome 2.cbz","Tome 3.cbz"])";
            const bool held = request.contains("\"root\":\"Gard");
            return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],"toSend":)"
                                   + (held ? QByteArray("[]") : three) + R"(,"alreadyThere":)"
                                   + (held ? three : QByteArray("[]")) + R"(,"bytesToSend":9})");
        };
        m_offers->offer({aCompleteSeries(u"Prête"_s), aCompleteSeries(u"Gardée"_s),
                          aKomgaSeriesNamed(u"Komga"_s)});
        QTRY_COMPARE(m_imports->reviewing(), 1);
        QTRY_COMPARE(readyCards(), 2);
        m_review->view()->select(u"all"_s);
        QCOMPARE(m_review->toReview(), 1);

        QVERIFY(m_review->view()->reviewNext());
        QCOMPARE(cardShown(), idNamed(u"Komga"_s));
        QVERIFY(m_review->view()->reviewNext());
        QCOMPARE(cardShown(), idNamed(u"Komga"_s));
    }

    /// The walk goes to the next one without undoing what the reader narrowed the list to.
    void the_next_one_keeps_the_filter_and_the_search()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aKomgaSeriesNamed(u"Komga A"_s), aKomgaSeriesNamed(u"Komga B"_s),
                          aCompleteSeries()});
        QTRY_COMPARE(m_imports->reviewing(), 2);
        m_review->view()->select(u"all"_s);
        m_review->view()->setFilter(int(Review::Filter::ToReview));
        m_review->view()->setSearch(u"komga"_s);

        QVERIFY(m_review->view()->reviewNext());
        QCOMPARE(cardShown(), idNamed(u"Komga A"_s));
        QCOMPARE(m_review->view()->filter(), int(Review::Filter::ToReview));
        QCOMPARE(m_review->view()->search(), u"komga"_s);
        QVERIFY(m_review->view()->reviewNext());
        QCOMPARE(cardShown(), idNamed(u"Komga B"_s));
        QCOMPARE(m_review->view()->filter(), int(Review::Filter::ToReview));
        QCOMPARE(m_review->view()->search(), u"komga"_s);
    }

    /// With nothing left to review, the walk is off and moves nothing.
    void nothing_left_to_review_turns_the_walk_off()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aCompleteSeries(), aCompleteSeries(u"Mira"_s)});
        QTRY_COMPARE(readyCards(), 2);
        m_review->view()->select(idOf(1, {}));
        QCOMPARE(m_review->toReview(), 0);
        QSignalSpy changed(m_review, &Review::changed);
        QVERIFY(!m_review->view()->reviewNext());
        QCOMPARE(changed.size(), 0);
        QCOMPARE(cardShown(), idOf(1, {}));
    }

    /// Thirty cards, three of them universes, open as a tree of five lines a reader can
    /// read, not a wall of sixty: the universes folded, « Sans univers » folded too unless
    /// what the frame opens on is in it.
    void fifty_series_open_with_their_universes_folded()
    {
        QStringList paths;
        for (int one = 0; one < 3; ++one) {
            const QString at = m_folder.filePath(u"Monde %1"_s.arg(one));
            QVERIFY(QDir().mkpath(at));
            QFile declaration(at + u"/universe.json"_s);
            QVERIFY(declaration.open(QIODevice::WriteOnly));
            declaration.write(R"({"leaf":1,"name":"Monde )" + QByteArray::number(one)
                              + R"("})");
            declaration.close();
            for (const QString &series : {u"Est"_s, u"Ouest"_s}) {
                QVERIFY(QDir().mkpath(at + u'/' + series));
                aDeclaration(at + u'/' + series, series.toUtf8());
                aVolume(at + u'/' + series + u"/Tome 1.cbz"_s, 1);
            }
            paths << at;
        }
        for (int one = 0; one < 27; ++one)
            paths << aCompleteSeries(u"Lot %1"_s.arg(one));
        m_pretend->answerFor = sessions();
        m_offers->offer(paths);
        QTRY_COMPARE_WITH_TIMEOUT(readyCards(), 30, 60000);

        m_review->view()->open();
        QCOMPARE(railLine(u"all"_s).value(u"expanded"_s).toBool(), true);
        for (int one = 0; one < 3; ++one) {
            const QVariantMap universe = railLine(idOf(rowNamed(u"Monde %1"_s.arg(one)), {}));
            QVERIFY(!universe.isEmpty());
            QCOMPARE(universe.value(u"expanded"_s).toBool(), false);
        }
        QCOMPARE(railLine(u"loose"_s).value(u"expanded"_s).toBool(), false);
        QCOMPARE(m_review->rail().size(), 5);

        // What the frame opens on is in « Sans univers »: that one is open, the rest stays.
        QVERIFY(m_review->edits()->edit(idOf(rowNamed(u"Lot 3"_s), {}), u"title"_s, QString()));
        m_review->view()->open();
        QCOMPARE(m_review->sheet().value(u"id"_s).toString(), idOf(rowNamed(u"Lot 3"_s), {}));
        QCOMPARE(railLine(u"loose"_s).value(u"expanded"_s).toBool(), true);
        for (int one = 0; one < 3; ++one)
            QCOMPARE(railLine(idOf(rowNamed(u"Monde %1"_s.arg(one)), {}))
                         .value(u"expanded"_s)
                         .toBool(),
                     false);
    }

    /// One universe of two series opens whole: folded, the one node there was hid the two
    /// series it was dropped for.
    void a_lone_universe_opens_unfolded()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverse()});
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->toggle(idOf(0, {}));
        m_review->view()->open();
        QCOMPARE(railLine(idOf(0, {})).value(u"expanded"_s).toBool(), true);
        QVERIFY(!railLine(idOf(0, u"Elfes"_s)).isEmpty());
        QVERIFY(!railLine(idOf(0, u"Mages"_s)).isEmpty());
    }

    /// The frame opens on what « Revoir la suivante » would reach first: the tree's first
    /// card to review. Opened in the order things were dropped instead, a series without
    /// universe dropped first was opened, below a universe the tree drew above it.
    void the_frame_opens_on_the_first_card_to_review_in_the_tree()
    {
        const QString universe = m_folder.filePath(u"Terres"_s);
        QVERIFY(QDir().mkpath(universe + u"/Elfes"_s));
        QFile declaration(universe + u"/universe.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"name":"Terres"})");
        declaration.close();
        QFile series(universe + u"/Elfes/work.json"_s);
        QVERIFY(series.open(QIODevice::WriteOnly));
        series.write(R"({"leaf":1,"title":"Elfes"})");
        series.close();
        aVolume(universe + u"/Elfes/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = sessions();
        m_offers->offer({aKomgaSeriesNamed(u"Komga"_s), universe});
        QTRY_COMPARE(m_imports->reviewing(), 2);

        m_review->view()->open();
        QCOMPARE(cardShown(), idNamed(u"Terres"_s));
        m_review->view()->select(u"all"_s);
        QVERIFY(m_review->view()->reviewNext());
        QCOMPARE(cardShown(), idNamed(u"Terres"_s));
    }

    /// One session per announcement asking for every file announced, a transfer never
    /// answered — what « Importer » lets go stays on its way, where a test can see it — and
    /// `refused`'s announcement refused.
    std::function<QByteArray(const QByteArray &)> sending(const QByteArray &refused = {})
    {
        return [this, refused](const QByteArray &request) {
            if (request.startsWith("DELETE"))
                return aReply(204, {});
            if (!request.startsWith("POST /import "))
                return QByteArray();
            if (!refused.isEmpty() && request.contains("\"root\":\"" + refused + '"'))
                return aReply(400, R"({"error":"refused"})");
            const QJsonObject body =
                QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
            QJsonArray toSend;
            for (const QJsonValue &file : body.value(u"files"_s).toArray())
                toSend.append(file.toObject().value(u"path"_s));
            const QByteArray id = "imp_t" + QByteArray::number(++m_opened);
            return aReply(200, R"({"id":")" + id + R"(","root":"x","creates":[],"toSend":)"
                                   + QJsonDocument(toSend).toJson(QJsonDocument::Compact)
                                   + R"(,"alreadyThere":[],"bytesToSend":27})");
        };
    }

    /// Holds back the announcements of the folders whose name starts with `prefix`: walked,
    /// they wait for the server's answer for as long as a test needs them to.
    static std::function<bool(const QByteArray &)> announcing(const QByteArray &prefix)
    {
        return [prefix](const QByteArray &request) {
            return request.startsWith("POST /import ")
                   && request.contains("\"root\":\"" + prefix);
        };
    }

    /// A series whose two volumes claim one number: held back by what it says, whatever
    /// « Importer » does with the rest.
    QString aDoubledSeries(const QString &name)
    {
        const QString at = m_folder.filePath(name);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        aDeclaration(at, name.toUtf8());
        aVolume(at + u"/Tome 1.cbz"_s, 1);
        aVolume(at + u"/Tome 1 bis.cbz"_s, 1);
        return at;
    }

    bool walked(const QString &name) const
    {
        const int row = rowNamed(name);
        return row >= 0 && m_imports->cards().at(row).road.described;
    }

    /// Five series dropped, two read: « Importer » sends those two without waiting for the
    /// three, which go in their turn once read. At fifty series the walk lasts longer than
    /// the first look, and waiting for its end was the longest wait of the import.
    void importing_while_series_are_still_read_sends_what_is_read()
    {
        m_pretend->answerFor = sending();
        m_pretend->holdBack = announcing("Lent");
        m_offers->offer({aCompleteSeries(u"Prête A"_s), aCompleteSeries(u"Prête B"_s),
                          aCompleteSeries(u"Lent C"_s), aCompleteSeries(u"Lent D"_s),
                          aCompleteSeries(u"Lent E"_s)});
        QTRY_COMPARE(readyCards(), 2);
        for (const QString &name : {u"Lent C"_s, u"Lent D"_s, u"Lent E"_s})
            QTRY_VERIFY(walked(name));
        QVERIFY(m_review->importable());
        QCOMPARE(m_review->importLabel(), Words::importVolumes(6));

        m_queue->confirm();
        QTRY_VERIFY2(m_pretend->heard.contains("PUT /import/imp_t"), m_pretend->heard.constData());
        for (const QString &name : {u"Prête A"_s, u"Prête B"_s})
            QVERIFY(m_imports->cards().at(rowNamed(name)).road.confirmed);
        for (const QString &name : {u"Lent C"_s, u"Lent D"_s, u"Lent E"_s})
            QCOMPARE(stageOf(rowNamed(name)), int(Imports::Stage::Asking));

        m_pretend->holdBack = nullptr;
        m_pretend->release();
        for (const QString &name : {u"Lent C"_s, u"Lent D"_s, u"Lent E"_s}) {
            QTRY_COMPARE(stageOf(rowNamed(name)), int(Imports::Stage::Ready));
            QVERIFY2(m_imports->cards().at(rowNamed(name)).road.confirmed, qPrintable(name));
        }
        // Waiting their turn behind the one on its way, not offered to « Importer » again.
        QVERIFY(!m_review->importable());
    }

    /// A blocked series, two still read and a refused one: the footer says who waits and
    /// why, and that the others go without them — read as the reason nothing went, the
    /// footer of fifty series kept a reader waiting for three.
    void the_footer_says_who_waits_and_that_they_hold_back_no_one()
    {
        m_pretend->answerFor = sending("Refus");
        m_pretend->holdBack = announcing("Lent");
        m_offers->offer({aDoubledSeries(u"Doublons"_s), aCompleteSeries(u"Lent A"_s),
                          aCompleteSeries(u"Lent B"_s), aCompleteSeries(u"Refus"_s)});
        QTRY_COMPARE(stageOf(rowNamed(u"Doublons"_s)), int(Imports::Stage::Reviewing));
        QTRY_COMPARE(stageOf(rowNamed(u"Refus"_s)), int(Imports::Stage::Failed));
        QTRY_VERIFY(walked(u"Lent A"_s) && walked(u"Lent B"_s));
        m_review->view()->select(u"all"_s);

        const QString waiting = Words::waitingSeries(1, 2, 1);
        QVERIFY(!waiting.isEmpty());
        QVERIFY2(m_review->footer().startsWith(waiting + u' '), qPrintable(m_review->footer()));

        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QTRY_COMPARE(readyCards(), 2);
        QVERIFY2(m_review->footer().startsWith(Words::waitingSeries(1, 0, 1) + u' '),
                 qPrintable(m_review->footer()));
    }

    /// The frame opened while the drop is walked says how far the walk has come, and the
    /// tree takes each folder as soon as it is read, not when the server has answered it.
    void the_frame_counts_the_folders_read_while_it_fills()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aCompleteSeries(u"Un"_s)});
        QTRY_COMPARE(readyCards(), 1);
        const QString alone = m_review->subtitle();

        // Nothing fingerprinted: no count of the walk says anything to the frame on the way,
        // and only the walk's end can tell it a folder was read.
        m_imports->setVerifying(false);
        m_pretend->holdBack = announcing("D");
        m_offers->offer({aCompleteSeries(u"Deux"_s), aCompleteSeries(u"Dix"_s)});
        // Nothing of the two walked yet: one read, two to read.
        QCOMPARE(m_imports->foldersRead(), 1);
        QCOMPARE(m_imports->foldersToRead(), 2);
        QCOMPARE(m_review->subtitle(), Words::whileRead(alone, 1, 3));
        QVERIFY(m_review->subtitle() != alone);
        QVERIFY(railLine(idNamed(u"Deux"_s)).isEmpty());

        // The screen redraws the tree and the count when the frame says it changed, and the
        // walk's end is the only news there is: unsaid, both stayed as they were until the
        // server answered.
        QSignalSpy told(m_review, &Review::changed);
        QTRY_COMPARE(m_imports->foldersRead(), 3);
        QCOMPARE(m_imports->foldersToRead(), 0);
        QVERIFY2(told.size() >= 2, qPrintable(QString::number(told.size())));
        // Walked, and their announcements still unanswered: in the tree all the same.
        QVERIFY(!railLine(idNamed(u"Deux"_s)).isEmpty());
        QVERIFY(!railLine(idNamed(u"Dix"_s)).isEmpty());
        QVERIFY(!m_review->subtitle().endsWith(Words::foldersRead(3, 3)));
        QVERIFY(!m_review->subtitle().endsWith(Words::foldersRead(1, 3)));
    }

    /// An edition under a series that says its direction shows that direction, and says the
    /// status its own sidecar wrote: both read « Non renseigné » beside an « Écrit » chip.
    void an_edition_shows_the_direction_and_the_status_it_inherits_or_says()
    {
        const QString series = m_folder.filePath(u"Serie"_s);
        const QString edition = series + u"/Ed"_s;
        QVERIFY(QDir().mkpath(edition));
        aDeclaration(series, "Serie");
        QFile sidecar(edition + u"/edition.json"_s);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(R"({"leaf":1,"name":"Ed","status":"completed","volumeCount":1})");
        sidecar.close();
        aVolume(edition + u"/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_e","root":"Serie","toSend":["Ed/Tome 1.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":10,"creates":[]})");
        };
        m_offers->offer({series});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        m_review->view()->open();
        m_review->view()->select(idOf(0, u"Ed"_s));
        const QVariantMap direction = fieldOnSheet(u"readingDirection"_s);
        const QVariantMap status = fieldOnSheet(u"status"_s);
        QCOMPARE(direction.value(u"text"_s).toString(), Words::readingDirection(Api::ReadingDirection::RightToLeft));
        QCOMPARE(status.value(u"text"_s).toString(), Words::editionStatus(u"completed"_s));
    }

    /// A volume's chapters are replaced by `setChapters`, a line that was there keeping what the
    /// window does not show, and what the format would refuse is refused with nothing changed.
    void a_volume_sheet_replaces_its_chapters()
    {
        aKomgaSeriesAnnounced();
        const QString id = idOf(0, komgaName(13));
        m_review->view()->select(id);
        QVERIFY(m_review->sheet().contains(u"chaptersHeld"_s));
        QVERIFY(m_review->sheet().value(u"chaptersEditable"_s).toBool());
        const auto row = [](const QString &number, const QString &title, const QString &page,
                            int source) {
            return QVariantMap{{u"number"_s, number}, {u"title"_s, title}, {u"page"_s, page},
                               {u"source"_s, source}};
        };
        // A number missing, twice, not a number; a page that is not whole: each refused.
        QVERIFY(!m_review->edits()->setChapters(id, {row(u""_s, u"A"_s, u"1"_s, -1)}));
        QVERIFY(!m_review->edits()->setChapters(id, {row(u"1"_s, u"A"_s, u""_s, -1), row(u"1"_s, u"B"_s, u""_s, -1)}));
        QVERIFY(!m_review->edits()->setChapters(id, {row(u"un"_s, u"A"_s, u""_s, -1)}));
        QVERIFY(!m_review->edits()->setChapters(id, {row(u"1"_s, u"A"_s, u"4,5"_s, -1)}));

        QVERIFY(m_review->edits()->setChapters(id, {row(u"7"_s, u"Le duel"_s, u"12"_s, -1),
                                           row(u"8"_s, QString(), QString(), -1)}));
        const QVariantList held = m_review->sheet().value(u"chaptersHeld"_s).toList();
        QCOMPARE(held.size(), 2);
        QCOMPARE(held.at(0).toMap().value(u"title"_s).toString(), u"Le duel"_s);
        QCOMPARE(held.at(0).toMap().value(u"page"_s).toString(), u"12"_s);
        QVERIFY(held.at(1).toMap().value(u"page"_s).toString().isEmpty());
    }

    /// The window of a volume's chapters says how many they are and over how many pages, and
    /// shows where the series' arcs cross them: one already running when the volume opens,
    /// said with its whole span, and one opening at its chapter, said with its page. The arcs
    /// are read off the series and changed there, where `chaptersWork` leads.
    void a_volume_sheet_says_its_chapters_and_the_arcs_that_cross_them()
    {
        aDragonBallOfThreeVolumes();
        m_review->view()->select(idOf(0, u"Tome 2.cbz"_s));
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"chaptersPages"_s).toInt(), 1);
        QCOMPARE(sheet.value(u"chaptersWork"_s).toString(), idOf(0, QString()));
        const QVariantList arcs = sheet.value(u"chaptersArcs"_s).toList();
        QCOMPARE(arcs.size(), 2);
        const QVariantMap running = arcs.at(0).toMap();
        QCOMPARE(running.value(u"at"_s).toInt(), 0);
        QCOMPARE(running.value(u"name"_s).toString(), u"Saga des Dragon Balls"_s);
        QCOMPARE(running.value(u"state"_s).toString(), Words::chaptersArcState(false));
        QCOMPARE(running.value(u"said"_s).toString(), Words::chaptersArcRuns(1, 14, 3));
        QVERIFY(!running.value(u"starts"_s).toBool());
        const QVariantMap opening = arcs.at(1).toMap();
        // Chapter 9 is the third of the volume's, and starts on its page 34.
        QCOMPARE(opening.value(u"at"_s).toInt(), 2);
        QCOMPARE(opening.value(u"name"_s).toString(), u"Saga de Piccolo"_s);
        QCOMPARE(opening.value(u"state"_s).toString(), Words::chaptersArcState(true));
        QCOMPARE(opening.value(u"said"_s).toString(), Words::chaptersArcStarts(9, 34));
        QVERIFY(opening.value(u"starts"_s).toBool());

        // The first volume opens the saga of the balls itself, and Piccolo is not in it.
        m_review->view()->select(idOf(0, u"Tome 1.cbz"_s));
        const QVariantList first = m_review->sheet().value(u"chaptersArcs"_s).toList();
        QCOMPARE(first.size(), 1);
        QCOMPARE(first.at(0).toMap().value(u"said"_s).toString(), Words::chaptersArcStarts(1, 2));
    }

    /// A series that names no form for its chapters shows the one the server gives them, in
    /// grey as an inherited value: an empty box said nothing of « Chap. {n} », which is what
    /// every chapter without a title of its own would be called.
    void a_series_without_a_chapter_label_shows_the_default_greyed()
    {
        aDragonBallOfThreeVolumes();
        m_review->view()->select(idOf(0, QString()));
        const QVariantMap label = fieldOnSheet(u"chapterLabel"_s);
        QVERIFY(label.value(u"text"_s).toString().isEmpty());
        QVERIFY(label.value(u"inherited"_s).toBool());
        QCOMPARE(label.value(u"inheritedText"_s).toString(), Words::defaultChapterLabel());
        // And what the empty box shows: « Non renseigné » said nothing of what every
        // chapter without a title would be called.
        QCOMPARE(label.value(u"placeholder"_s).toString(), Words::defaultChapterLabel());
        QVERIFY(!label.value(u"lacking"_s).toBool());
        // How a form is written, said under it.
        QCOMPARE(label.value(u"hint"_s).toString(), Words::chapterLabelHint());

        // Said, it is the series' own.
        QVERIFY(m_review->edits()->edit(idOf(0, QString()), u"chapterLabel"_s, u"Chap.{n:00}"_s));
        QVERIFY(!fieldOnSheet(u"chapterLabel"_s).value(u"inherited"_s).toBool());
    }

    /// A list field offers the names the library already holds, after those of the import:
    /// « Toriyama » typed on a new series offered nothing while ten series of the library had
    /// him, and a name retyped with a typo is a second author.
    void a_list_field_offers_the_library_s_names_after_the_import_s()
    {
        aDragonBallOfThreeVolumes();
        m_review->view()->select(idOf(0, QString()));
        const QStringList before =
            fieldOnSheet(u"authors"_s).value(u"vocabulary"_s).toStringList();

        m_review->edits()->setLibraryWords({{u"authors"_s, QStringList{u"Goscinny"_s}},
                                   {u"artists"_s, QStringList{u"Moebius"_s, u"toriyama"_s}},
                                   {u"genres"_s, QStringList{u"Western"_s}},
                                   {u"tags"_s, QStringList{u"Classique"_s}}});
        const QStringList writer =
            fieldOnSheet(u"authors"_s).value(u"vocabulary"_s).toStringList();
        // The import's first, in their order; then the library's, once each: whoever writes
        // may draw.
        QCOMPARE(writer.mid(0, before.size()), before);
        QVERIFY(writer.contains(u"Goscinny"_s));
        QVERIFY(writer.contains(u"Moebius"_s));
        // Met in the import and in the library alike: offered once.
        // Met in the import and in the library alike, spelled otherwise: offered once, as the
        // import spells it.
        QVERIFY(before.contains(u"Toriyama"_s));
        QCOMPARE(writer.filter(u"toriyama"_s, Qt::CaseInsensitive).size(), 1);
        QVERIFY(fieldOnSheet(u"genres"_s).value(u"vocabulary"_s).toStringList()
                    .contains(u"Western"_s));
        QVERIFY(fieldOnSheet(u"tags"_s).value(u"vocabulary"_s).toStringList()
                    .contains(u"Classique"_s));
        // A genre is not offered as a tag.
        QVERIFY(!fieldOnSheet(u"tags"_s).value(u"vocabulary"_s).toStringList()
                     .contains(u"Western"_s));
    }

    /// A chapter goes where the page it starts on puts it: before the first other line that
    /// starts further on, or after the last one that starts before it, so that a line still
    /// without a page stays where it was, and so does a line whose page is taken away.
    void a_chapter_goes_where_its_first_page_puts_it()
    {
        const auto row = [](const QString &page) {
            return QVariantMap{{u"number"_s, u"1"_s}, {u"title"_s, QString()}, {u"page"_s, page},
                               {u"source"_s, -1}};
        };
        // 2, 34, 50 and 66, the second line typed 60.
        const QVariantList typed{row(u"2"_s), row(u"60"_s), row(u"50"_s), row(u"66"_s)};
        QCOMPARE(m_review->edits()->chapterPlace(typed, 1), 2);
        // The first line typed past every other.
        QCOMPARE(m_review->edits()->chapterPlace({row(u"90"_s), row(u"34"_s), row(u"50"_s)}, 0), 2);
        // The last line typed before every other.
        QCOMPARE(m_review->edits()->chapterPlace({row(u"2"_s), row(u"34"_s), row(u"1"_s)}, 2), 0);
        // Where it already is.
        QCOMPARE(m_review->edits()->chapterPlace({row(u"2"_s), row(u"34"_s), row(u"50"_s)}, 1), 1);
        // A new line at the end goes among the lines with a page, ahead of those without.
        QCOMPARE(m_review->edits()->chapterPlace({row(u"2"_s), row(u"50"_s), row(QString()), row(u"34"_s)}, 3), 1);
        QCOMPARE(m_review->edits()->chapterPlace({row(u"2"_s), row(QString()), row(u"60"_s)}, 2), 2);
        // No page, no move.
        QCOMPARE(m_review->edits()->chapterPlace({row(u"50"_s), row(QString()), row(u"2"_s)}, 1), 1);
    }

    /// The chapters follow their order: numbered again from the number asked, each the one
    /// before it plus one — what « Numéroter à partir de » does, and a drag after it. A first
    /// number that is not one changes nothing.
    void the_chapters_are_numbered_from_the_first_number_asked()
    {
        const auto row = [](const QString &number, const QString &title, int source) {
            return QVariantMap{{u"number"_s, number}, {u"title"_s, title}, {u"page"_s, u"2"_s},
                               {u"source"_s, source}};
        };
        const QVariantList rows{row(u"1"_s, u"A"_s, 0), row(u"45.5"_s, u"B"_s, 1),
                                row(QString(), u"C"_s, -1)};
        const QVariantList numbered = m_review->edits()->chaptersNumbered(rows, u"15"_s);
        QCOMPARE(numbered.size(), 3);
        QCOMPARE(numbered.at(0).toMap().value(u"number"_s).toString(), u"15"_s);
        QCOMPARE(numbered.at(1).toMap().value(u"number"_s).toString(), u"16"_s);
        QCOMPARE(numbered.at(2).toMap().value(u"number"_s).toString(), u"17"_s);
        // Everything else of a line stays as it was.
        QCOMPARE(numbered.at(1).toMap().value(u"title"_s).toString(), u"B"_s);
        QCOMPARE(numbered.at(1).toMap().value(u"source"_s).toInt(), 1);
        QCOMPARE(m_review->edits()->chaptersNumbered(rows, u"quinze"_s), rows);
        QCOMPARE(m_review->edits()->chaptersNumbered(rows, u"12,5"_s).at(1).toMap().value(u"number"_s)
                     .toString(), Words::number(13.5));
    }

    // ——— Sagas as separators ————————————————————————————————————————————————————

    static QVariantMap anArc(const QString &name, int from, int to, const QString &parent = {},
                             const QString &unit = u"CHAPTER"_s)
    {
        return {{u"name"_s, name}, {u"parent"_s, parent}, {u"unit"_s, unit},
                {u"from"_s, from}, {u"to"_s, to}};
    }

    /// Dragon Ball as the drawing of the window has it, in the order the file writes it: each
    /// saga ahead of its arcs, with the range of all of them.
    static QVariantList dragonBall()
    {
        const QString kid = u"Son Gokû enfant"_s;
        const QString saiyans = u"Saiyans et Freezer"_s;
        return {anArc(u"Prologue"_s, 1, 1),
                anArc(kid, 2, 194),
                anArc(u"Recherche des boules"_s, 2, 23, kid),
                anArc(u"Armée du Ruban rouge"_s, 55, 112, kid),
                anArc(u"Tournoi"_s, 136, 194, kid),
                anArc(u"Le tournoi de Piccolo"_s, 195, 202),
                anArc(saiyans, 203, 329),
                anArc(u"Saiyans"_s, 203, 245, saiyans),
                anArc(u"Freezer"_s, 246, 329, saiyans)};
    }

    /// The row of `kind` named `name` — or, for a gap or « Aucune saga », the first of them.
    static int rowAt(const QVariantList &rows, const QString &kind, const QString &name = {})
    {
        for (int at = 0; at < rows.size(); ++at) {
            const QVariantMap row = rows.at(at).toMap();
            if (row.value(u"kind"_s).toString() == kind
                && (name.isEmpty() || row.value(u"name"_s).toString() == name))
                return at;
        }
        return -1;
    }

    static QString parentOf(const QVariantList &arcs, const QString &name)
    {
        for (const QVariant &one : arcs)
            if (one.toMap().value(u"name"_s).toString() == name)
                return one.toMap().value(u"parent"_s).toString();
        return u"<absent>"_s;
    }

    void arcs_before_the_first_saga_have_none()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        QCOMPARE(rows.value(0).toMap().value(u"kind"_s).toString(), u"arc"_s);
        QCOMPARE(rows.value(0).toMap().value(u"name"_s).toString(), u"Prologue"_s);
        QCOMPARE(rows.value(1).toMap().value(u"kind"_s).toString(), u"saga"_s);
        QCOMPARE(rows.value(1).toMap().value(u"name"_s).toString(), u"Son Gokû enfant"_s);
    }

    /// The saga's range is its arcs', not one typed beside them: 2–194 over three arcs.
    void a_saga_spans_its_arcs_and_counts_them()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const QVariantMap kid = rows.value(rowAt(rows, u"saga"_s, u"Son Gokû enfant"_s)).toMap();
        QCOMPARE(kid.value(u"count"_s).toInt(), 3);
        QCOMPARE(kid.value(u"from"_s).toInt(), 2);
        QCOMPARE(kid.value(u"to"_s).toInt(), 194);
        QCOMPARE(kid.value(u"unit"_s).toString(), u"CHAPTER"_s);
    }

    /// Chapters no arc covers are a line of their own, where they fall — and the chapter
    /// right after an arc's last is no hole.
    void a_hole_between_two_arcs_is_a_line()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const int ruban = rowAt(rows, u"arc"_s, u"Armée du Ruban rouge"_s);
        const QVariantMap gap = rows.value(ruban + 1).toMap();
        QCOMPARE(gap.value(u"kind"_s).toString(), u"gap"_s);
        QCOMPARE(gap.value(u"from"_s).toInt(), 113);
        QCOMPARE(gap.value(u"to"_s).toInt(), 135);
        QCOMPARE(rows.value(ruban + 2).toMap().value(u"name"_s).toString(), u"Tournoi"_s);
        // Prologue ends on 1, the next arc opens on 2: nothing between them.
        QCOMPARE(rows.value(2).toMap().value(u"name"_s).toString(), u"Recherche des boules"_s);
    }

    void an_arc_without_saga_after_a_saga_opens_none()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const int piccolo = rowAt(rows, u"arc"_s, u"Le tournoi de Piccolo"_s);
        QCOMPARE(rows.value(piccolo - 1).toMap().value(u"kind"_s).toString(), u"none"_s);
    }

    void rows_write_back_what_they_read()
    {
        QCOMPARE(Sagas::arcsOf(Sagas::rowsOf(dragonBall())), dragonBall());
        // And through the sheet, the way the window calls it.
        QCOMPARE(m_review->edits()->arcsFromRows(m_review->edits()->arcRows(dragonBall())), dragonBall());
    }

    /// An entry with a `parent` is an arc, even named as its saga is: Water Seven is a saga of
    /// One Piece and an arc of it, the window drew only the saga, and « Garder les arcs »
    /// wrote the file back without the arc — which the server reads and keeps.
    void an_arc_named_as_its_saga_is_kept()
    {
        const QVariantList file{anArc(u"Water Seven"_s, 1, 40),
                                anArc(u"Water Seven"_s, 1, 20, u"Water Seven"_s),
                                anArc(u"Enies Lobby"_s, 21, 40, u"Water Seven"_s)};
        const QVariantList rows = Sagas::rowsOf(file);
        QVERIFY(rowAt(rows, u"saga"_s, u"Water Seven"_s) >= 0);
        QVERIFY(rowAt(rows, u"arc"_s, u"Water Seven"_s) >= 0);
        const QVariantList back = Sagas::arcsOf(rows);
        QCOMPARE(back.size(), 3);
        int named = 0;
        for (const QVariant &one : back)
            named += one.toMap().value(u"name"_s).toString() == u"Water Seven"_s;
        QCOMPARE(named, 2);
    }

    /// A saga interrupted in the chapters — the file says it once, its arcs on either side of
    /// one that is not its — reads as two separators of one name, and writes back once.
    void a_saga_split_in_the_file_reads_twice_and_writes_once()
    {
        const QVariantList file{anArc(u"S"_s, 1, 40), anArc(u"a"_s, 1, 10, u"S"_s),
                                anArc(u"b"_s, 30, 40, u"S"_s), anArc(u"t"_s, 11, 29)};
        const QVariantList rows = Sagas::rowsOf(file);
        int sagas = 0;
        for (const QVariant &row : rows)
            sagas += row.toMap().value(u"kind"_s).toString() == u"saga"_s;
        QCOMPARE(sagas, 2);

        const QVariantList back = Sagas::arcsOf(rows);
        QCOMPARE(back.size(), 4);
        int named = 0;
        for (const QVariant &one : back)
            named += one.toMap().value(u"name"_s).toString() == u"S"_s;
        QCOMPARE(named, 1);
        QCOMPARE(parentOf(back, u"a"_s), u"S"_s);
        QCOMPARE(parentOf(back, u"b"_s), u"S"_s);
        QCOMPARE(parentOf(back, u"t"_s), QString());
        for (const QVariant &one : back) {
            if (one.toMap().value(u"name"_s).toString() == u"S"_s) {
                QCOMPARE(one.toMap().value(u"from"_s).toInt(), 1);
                QCOMPARE(one.toMap().value(u"to"_s).toInt(), 40);
            }
        }
    }

    /// Down one arc, the saga leaves its first arc above it: « Recherche des boules » goes to
    /// what is above, no saga.
    void moving_a_separator_down_hands_it_the_next_arc()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const QVariantList after =
            Sagas::moved(rows, rowAt(rows, u"saga"_s, u"Son Gokû enfant"_s), +1);
        const QVariantList arcs = Sagas::arcsOf(after);
        QCOMPARE(parentOf(arcs, u"Recherche des boules"_s), QString());
        QCOMPARE(parentOf(arcs, u"Armée du Ruban rouge"_s), u"Son Gokû enfant"_s);
        // Back up one arc, it takes it again.
        const QVariantList back =
            Sagas::moved(after, rowAt(after, u"saga"_s, u"Son Gokû enfant"_s), -1);
        QCOMPARE(parentOf(Sagas::arcsOf(back), u"Recherche des boules"_s), u"Son Gokû enfant"_s);
    }

    /// « Aucune saga » moved below Piccolo stands against « Saiyans et Freezer »: once more
    /// down would pass it, and it stays where it is. Nor does the first line go higher.
    void a_separator_never_jumps_another()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const QVariantList once = Sagas::moved(rows, rowAt(rows, u"none"_s), +1);
        QCOMPARE(parentOf(Sagas::arcsOf(once), u"Le tournoi de Piccolo"_s), u"Son Gokû enfant"_s);
        QCOMPARE(Sagas::moved(once, rowAt(once, u"none"_s), +1), once);
        const QVariantList top{QVariantMap{{u"kind"_s, u"saga"_s}, {u"name"_s, u"S"_s}},
                               QVariantMap{{u"kind"_s, u"arc"_s}, {u"name"_s, u"a"_s},
                                           {u"unit"_s, u"CHAPTER"_s}, {u"from"_s, 1},
                                           {u"to"_s, 2}}};
        QCOMPARE(Sagas::moved(top, 0, -1), top);
    }

    /// Without its separator, a saga's arcs belong to what is above: « Aucune saga » taken
    /// out gives Piccolo to « Son Gokû enfant »; « Saiyans et Freezer » taken out gives its
    /// two arcs to « Aucune saga ».
    void removing_a_saga_gives_its_arcs_to_the_one_above()
    {
        const QVariantList rows = Sagas::rowsOf(dragonBall());
        const QVariantList noNone = Sagas::removed(rows, rowAt(rows, u"none"_s));
        QCOMPARE(parentOf(Sagas::arcsOf(noNone), u"Le tournoi de Piccolo"_s), u"Son Gokû enfant"_s);

        const QVariantList noSaiyans =
            Sagas::removed(rows, rowAt(rows, u"saga"_s, u"Saiyans et Freezer"_s));
        const QVariantList arcs = Sagas::arcsOf(noSaiyans);
        QCOMPARE(parentOf(arcs, u"Saiyans"_s), QString());
        QCOMPARE(parentOf(arcs, u"Freezer"_s), QString());
        QCOMPARE(parentOf(arcs, u"Saiyans et Freezer"_s), u"<absent>"_s);
    }

    /// « Tournoi » corrected to open on chapter 240 sorts among the Saiyans' arcs, and is
    /// theirs; the gap it leaves is worked out again.
    void an_arc_whose_start_changes_joins_the_saga_it_falls_in()
    {
        QVariantList rows = Sagas::rowsOf(dragonBall());
        const int tournoi = rowAt(rows, u"arc"_s, u"Tournoi"_s);
        QVariantMap changed = rows.at(tournoi).toMap();
        changed.insert(u"from"_s, 240);
        changed.insert(u"to"_s, 244);
        rows[tournoi] = changed;

        const QVariantList after = m_review->edits()->arcRowsEdited(rows, u"regap"_s, -1, 0);
        QCOMPARE(parentOf(Sagas::arcsOf(after), u"Tournoi"_s), u"Saiyans et Freezer"_s);
        const QVariantMap kid = after.value(rowAt(after, u"saga"_s, u"Son Gokû enfant"_s)).toMap();
        QCOMPARE(kid.value(u"count"_s).toInt(), 2);
        QCOMPARE(kid.value(u"to"_s).toInt(), 112);
        const int ruban = rowAt(after, u"arc"_s, u"Armée du Ruban rouge"_s);
        QCOMPARE(after.value(ruban + 1).toMap().value(u"kind"_s).toString(), u"gap"_s);
        QCOMPARE(after.value(ruban + 1).toMap().value(u"to"_s).toInt(), 194);
    }

    /// « + Un arc » adds a line at the foot whose start is not typed yet: sorted at once, it
    /// went to the top, under the hand that was typing in it at the foot. Sorting it waits for
    /// a start, and a line without one is not written.
    void a_line_half_typed_is_not_sorted_until_it_is_left()
    {
        QVariantList rows = Sagas::rowsOf(dragonBall());
        rows << QVariantMap{{u"kind"_s, u"arc"_s}, {u"name"_s, u"Cell"_s},
                            {u"unit"_s, u"CHAPTER"_s}, {u"from"_s, QString()},
                            {u"to"_s, QString()}};
        const QVariantList kept = m_review->edits()->arcRowsEdited(rows, u"regap"_s, -1, 0);
        QCOMPARE(kept.size(), rows.size());
        QCOMPARE(kept.constLast().toMap().value(u"name"_s).toString(), u"Cell"_s);
        QCOMPARE(Sagas::rowsOf(dragonBall()).constFirst().toMap().value(u"name"_s).toString(),
                 kept.constFirst().toMap().value(u"name"_s).toString());
        QVERIFY(m_review->edits()->arcsFromRows(kept).isEmpty());

        QVariantList typed = kept;
        QVariantMap cell = typed.takeLast().toMap();
        cell.insert(u"from"_s, u"150"_s);
        cell.insert(u"to"_s, u"160"_s);
        typed << cell;
        const QVariantList sorted = m_review->edits()->arcRowsEdited(typed, u"regap"_s, -1, 0);
        QCOMPARE(rowAt(sorted, u"arc"_s, u"Cell"_s) + 1, rowAt(sorted, u"none"_s));
        // Worded for the window, which draws them and counts nothing.
        QCOMPARE(sorted.at(rowAt(sorted, u"saga"_s)).toMap().value(u"said"_s).toString(),
                 Words::arcsSagaSpan(4, u"CHAPTER"_s, 2, 194));
        QCOMPARE(sorted.at(rowAt(sorted, u"gap"_s)).toMap().value(u"said"_s).toString(),
                 Words::arcsGap(24, 54));
    }

    /// The format has no saga of chapters and volumes at once, nor one without a name: the
    /// rows write nothing rather than a list the sheet would refuse.
    void a_saga_mixing_chapters_and_volumes_writes_nothing()
    {
        const QVariantList mixed{
            QVariantMap{{u"kind"_s, u"saga"_s}, {u"name"_s, u"S"_s}},
            QVariantMap{{u"kind"_s, u"arc"_s}, {u"name"_s, u"a"_s}, {u"unit"_s, u"CHAPTER"_s},
                        {u"from"_s, 1}, {u"to"_s, 2}},
            QVariantMap{{u"kind"_s, u"arc"_s}, {u"name"_s, u"b"_s}, {u"unit"_s, u"VOLUME"_s},
                        {u"from"_s, 1}, {u"to"_s, 1}}};
        QVERIFY(Sagas::arcsOf(mixed).isEmpty());
        QVariantList unnamed = mixed;
        unnamed.removeLast();
        unnamed[0] = QVariantMap{{u"kind"_s, u"saga"_s}, {u"name"_s, u" "_s}};
        QVERIFY(Sagas::arcsOf(unnamed).isEmpty());
        const QVariantList empty{QVariantMap{{u"kind"_s, u"saga"_s}, {u"name"_s, u"S"_s}}};
        QVERIFY(Sagas::arcsOf(empty).isEmpty());
    }

    /// Chapter 24 opens page 9 of the volume sold alone and page 120 of the one that binds two
    /// together: the place of an arc was looked for in every volume of the series and the
    /// first found was given, so a series of two editions said one edition's page under the
    /// other's volume. Each edition with volumes is a column, and its arc says its own pages.
    void an_arc_falls_in_each_edition_on_its_own_pages()
    {
        const QString series = m_folder.filePath(u"Dbz"_s);
        QVERIFY(QDir().mkpath(series + u"/Perfect"_s));
        QFile sidecar(series + u"/work.json"_s);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(R"({"leaf":1,"title":"Dbz","readingDirection":"RIGHT_TO_LEFT",)"
                      R"("authors":["Toriyama"],"artists":["Toriyama"],)"
                      R"("arcs":[{"name":"Tournoi","unit":"CHAPTER","from":24,"to":30}]})");
        sidecar.close();
        QFile edition(series + u"/Perfect/edition.json"_s);
        QVERIFY(edition.open(QIODevice::WriteOnly));
        edition.write(R"({"leaf":1,"name":"Perfect","readingDirection":"RIGHT_TO_LEFT"})");
        edition.close();
        const auto volume = [](const QString &path, int number, int opens, int ends) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(Zip::volume(R"({"leaf":1,"number":)" + QByteArray::number(number)
                                   + R"(,"chapters":[{"number":24,"startPage":)"
                                   + QByteArray::number(opens)
                                   + R"(},{"number":30,"startPage":)" + QByteArray::number(ends)
                                   + "}]}"));
        };
        volume(series + u"/Tome 3.cbz"_s, 3, 9, 150);
        volume(series + u"/Perfect/Tome 2.cbz"_s, 2, 120, 260);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Dbz", {u"Tome 3.cbz"_s, u"Perfect/Tome 2.cbz"_s}, {},
                                 R"([{"kind":"EDITION","name":"Perfect","at":"Perfect"}])");
            return QByteArray();
        };
        m_offers->offer({series});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        m_review->view()->open();
        m_review->view()->select(idOf(0, QString()));

        const QVariantList editions = m_review->sheet().value(u"arcsEditions"_s).toList();
        QCOMPARE(editions.size(), 2);
        // In the order of the tree, which lists a folder before the files beside it.
        QCOMPARE(editions.at(0).toMap().value(u"name"_s).toString(), u"Perfect"_s);
        QCOMPARE(editions.at(0).toMap().value(u"at"_s).toString(), u"Perfect"_s);
        QCOMPARE(editions.at(1).toMap().value(u"name"_s).toString(), u"Dbz"_s);
        QCOMPARE(editions.at(1).toMap().value(u"at"_s).toString(), QString());
        const QVariantList falls =
            m_review->sheet().value(u"arcs"_s).toList().at(0).toMap().value(u"falls"_s).toList();
        QCOMPARE(falls.size(), 2);
        QCOMPARE(falls.at(0).toString(),
                 Words::arcsFallsInChapters(u"tome 2"_s, 120, 30, u"tome 2"_s, 260));
        QCOMPARE(falls.at(1).toString(),
                 Words::arcsFallsInChapters(u"tome 3"_s, 9, 30, u"tome 3"_s, 150));
    }

    /// The arcs belong to the series' sheet: read from its `work.json`, kept apart from the
    /// fields, and replaced by `setArcs` — which refuses what the format would, changing nothing.
    void a_series_sheet_reads_and_replaces_its_arcs()
    {
        const QString series = m_folder.filePath(u"Serie"_s);
        QVERIFY(QDir().mkpath(series));
        QFile sidecar(series + u"/work.json"_s);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(R"({"leaf":1,"title":"Serie","readingDirection":"RIGHT_TO_LEFT",)"
                      R"("authors":["Toriyama"],"artists":["Toriyama"],)"
                      R"("chapterLabel":"Chap.{n:00}","arcs":[{"name":"Saga","unit":"CHAPTER",)"
                      R"("from":1,"to":20},{"name":"Duel","parent":"Saga","unit":"CHAPTER",)"
                      R"("from":5,"to":9}]})");
        sidecar.close();
        aVolume(series + u"/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_a","root":"Serie","toSend":["Tome 1.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":10,"creates":[]})");
        };
        m_offers->offer({series});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        m_review->view()->open();
        m_review->view()->select(idOf(0, QString()));
        QVERIFY(fieldOnSheet(u"arcs"_s).isEmpty());
        QCOMPARE(fieldOnSheet(u"chapterLabel"_s).value(u"text"_s).toString(), u"Chap.{n:00}"_s);
        const QVariantList arcs = m_review->sheet().value(u"arcs"_s).toList();
        QCOMPARE(arcs.size(), 2);
        QCOMPARE(arcs.at(1).toMap().value(u"parent"_s).toString(), u"Saga"_s);
        // No volume lists chapters here: an arc counted in chapters says nowhere, and one
        // counted in volumes says its volumes.
        const QVariantList nowhere = arcs.at(0).toMap().value(u"falls"_s).toList();
        QCOMPARE(nowhere.size(), 1);
        QVERIFY(nowhere.at(0).toString().isEmpty());
        QCOMPARE(Words::arcsFallsInVolumes(18, 31), u"tomes\u00A018 à 31"_s);
        QCOMPARE(Words::arcsFallsInVolumes(4, 4), u"tome\u00A04"_s);

        const auto arc = [](const QString &name, const QString &parent, const QString &unit,
                            int from, int to) {
            return QVariantMap{{u"name"_s, name}, {u"parent"_s, parent}, {u"unit"_s, unit},
                               {u"from"_s, from}, {u"to"_s, to}};
        };
        const QString id = idOf(0, QString());
        // Backwards, a name twice, a saga nobody is, an unknown unit: each refused, none kept.
        QVERIFY(!m_review->edits()->setArcs(id, {arc(u"A"_s, {}, u"CHAPTER"_s, 9, 5)}));
        QVERIFY(!m_review->edits()->setArcs(id, {arc(u"A"_s, {}, u"CHAPTER"_s, 1, 2),
                                        arc(u"A"_s, {}, u"VOLUME"_s, 1, 2)}));
        QVERIFY(!m_review->edits()->setArcs(id, {arc(u"A"_s, u"Z"_s, u"CHAPTER"_s, 1, 2)}));
        QVERIFY(!m_review->edits()->setArcs(id, {arc(u"A"_s, {}, u"PAGE"_s, 1, 2)}));
        QCOMPARE(m_review->sheet().value(u"arcs"_s).toList().size(), 2);

        QVERIFY(m_review->edits()->setArcs(id, {arc(u"Freezer"_s, {}, u"VOLUME"_s, 18, 31)}));
        const QVariantList kept = m_review->sheet().value(u"arcs"_s).toList();
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.at(0).toMap().value(u"unit"_s).toString(), u"VOLUME"_s);
    }

    /// The window says what a drop would send, and what the library already holds of it: a
    /// series held whole sends nothing and says how many of its files stay where they are.
    void a_series_held_whole_sends_nothing_and_says_what_is_held()
    {
        aHeldSeriesAnnounced(R"("creates":[])");
        QCOMPARE(m_tally->toSendFiles(), 0);
        QCOMPARE(m_tally->toSendBytes(), qint64(0));
        QCOMPARE(m_tally->heldFiles(), 14);
        QCOMPARE(Words::heldNotResent(14), u"14 fichiers déjà dans la bibliothèque, non renvoyés"_s);
        QVERIFY(Words::heldNotResent(0).isEmpty());
    }

    /// A series the library holds whole, and another dropped beside it not yet read: the
    /// frame offered « Fermer » over it, and closing let it go unseen.
    void a_drop_half_read_never_offers_to_close()
    {
        aHeldSeriesAnnounced(R"("creates":[])");
        QVERIFY(m_review->nothingToImport());

        m_offers->offer({aCompleteSeries()});
        QCOMPARE(m_imports->foldersToRead(), 1);
        QVERIFY(!m_review->nothingToImport());
        QVERIFY(m_review->importLabel() != Words::nothingToImport());
    }

    /// Ten series, three held back by what they say: « Importer » lets the seven others go,
    /// the queue to review is the three, and the frame closed and opened again opens on the
    /// first of them rather than on a series already on its way. The seven ask for nothing —
    /// numbers read off their names — and were to review when that was a question.
    void an_import_half_confirmed_closed_and_reopened_opens_on_what_is_left()
    {
        QStringList paths;
        for (int one = 0; one < 7; ++one)
            paths << aGuessedSeriesNamed(u"Komga %1"_s.arg(one));
        for (const QString &name : {u"Doublons A"_s, u"Doublons B"_s, u"Doublons C"_s})
            paths << aDoubledSeries(name);
        m_pretend->answerFor = sending();
        m_offers->offer(paths);
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), 3, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::all_of(paths, [this](const QString &path) {
                                     return walked(QFileInfo(path).fileName());
                                 }),
                                 30000);
        m_review->view()->open();
        QCOMPARE(m_review->toReview(), 3);

        m_queue->confirm();
        QTRY_COMPARE(m_review->toReview(), 3);
        m_review->view()->select(u"all"_s);
        m_review->view()->open();
        QCOMPARE(cardShown(), idNamed(u"Doublons A"_s));
    }

    /// « Suivant » beside a series that something stops walks to it rather than on: the first
    /// of them in the tree's order, its sheet on the screen — a series that waits for a value
    /// is not found by a reader who was sent to the next step.
    void showing_what_stops_a_series_walks_to_the_first_series_held_back()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aCompleteSeries(u"Prête"_s), aDoubledSeries(u"Doublons A"_s),
                          aDoubledSeries(u"Doublons B"_s)});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 2, 30000);
        m_review->view()->select(u"all"_s);

        QVERIFY(m_review->view()->showBlocked());
        QCOMPARE(cardShown(), idNamed(u"Doublons A"_s));
        // Nothing was prepared, let go or sent on the way.
        QCOMPARE(m_tally->launched(), 0);
    }

    /// With nothing held back it has nowhere to walk, and says so — the button then goes on.
    void showing_what_stops_a_series_says_so_when_nothing_does()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aCompleteSeries(u"Prête"_s)});
        QTRY_COMPARE(readyCards(), 1);
        QVERIFY(!m_review->view()->showBlocked());
    }

    /// A library is searched by who wrote and drew, and read in a direction: a series nobody
    /// is said to have written, nor drawn, or with no direction, waits for it — each of the
    /// three alone — and the others go. « Suivant » walks to the first of them, on its sheet.
    void a_series_with_no_writer_no_drawer_or_no_direction_waits_and_next_walks_to_it()
    {
        const QByteArray writer = R"j(,"authors":["Jarry"])j";
        const QByteArray drawer = R"j(,"artists":["Nesmo"])j";
        const QByteArray direction = R"j(,"readingDirection":"LEFT_TO_RIGHT")j";
        m_pretend->answerFor = sending();
        m_offers->offer({aSeriesSaying(u"Sans scénariste"_s, direction + drawer),
                          aSeriesSaying(u"Sans dessinateur"_s, direction + writer),
                          aSeriesSaying(u"Sans sens"_s, writer + drawer),
                          aSeriesSaying(u"Complète"_s, direction + writer + drawer)});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 3, 30000);
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->select(u"all"_s);

        QVERIFY(m_review->view()->showBlocked());
        QCOMPARE(cardShown(), idNamed(u"Sans scénariste"_s));
        QCOMPARE(m_review->sheet().value(u"notes"_s).toList().size(), 1);
        QCOMPARE(m_review->sheet().value(u"notes"_s).toList().constFirst().toMap()
                     .value(u"text"_s).toString(),
                 Words::creditLacking(u"authors"_s, 1));
        QCOMPARE(m_tally->launched(), 0);
        // One value lacks on each of the three, and the footer counts them as « Suivant » sees.
        QVERIFY2(m_review->footer().contains(Words::lackingValues(3)),
                 qPrintable(m_review->footer()));
    }

    /// The footer of a series that lacks its writer and its drawer says two values are
    /// missing, not « Rien ne manque », and typing them — as words on the series — is what
    /// lets it go: nothing else is asked.
    void the_footer_counts_a_writer_and_a_drawer_missing_and_typing_them_releases_the_series()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aSeriesSaying(u"Orpheline"_s, R"j(,"readingDirection":"LEFT_TO_RIGHT")j")});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 1, 30000);
        m_review->view()->open();

        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' ' + Words::reviewFooter(2, 0));
        QVERIFY(m_review->footer().contains(u"2 valeurs manquent encore"_s));
        const QString series = idOf(0, {});
        QVERIFY(m_review->edits()->editWords(series, u"authors"_s, {u"Jarry"_s}));
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' ' + Words::reviewFooter(1, 0));
        QVERIFY(m_review->edits()->editWords(series, u"artists"_s, {u"Nesmo"_s}));
        QTRY_COMPARE(m_tally->blocked(), 0);
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
        QVERIFY(!m_review->view()->showBlocked());
    }

    /// One volume credited and forty-one credited by nobody: the footer said « Rien ne manque »
    /// beside forty-one red volumes. It counts what is red where it is put right — the two
    /// credits, once, on the series — and never says nothing is missing while a volume is.
    void the_footer_counts_what_is_red_and_nothing_else()
    {
        m_pretend->answerFor = sending();
        const QString at = aSeriesSaying(u"Quarante"_s, R"j(,"readingDirection":"LEFT_TO_RIGHT")j");
        QFile first(at + u"/Tome 1.cbz"_s);
        QVERIFY(first.open(QIODevice::WriteOnly));
        first.write(Zip::volume(R"({"leaf":1,"number":1,"authors":["A"],"artists":["B"]})"));
        first.close();
        for (int number = 2; number <= 42; ++number)
            aVolume(at + u"/Tome "_s + QString::number(number) + u".cbz"_s, number);
        m_offers->offer({at});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 1, 30000);
        m_review->view()->open();

        // Its volumes are credited apart, so the footer is that list's: it still counts the two.
        QVERIFY2(m_review->footer().contains(Words::lackingValues(2)), qPrintable(m_review->footer()));
        QVERIFY(!m_review->footer().contains(Words::reviewFooter(0, 0)));
    }

    /// « Suivant » walks to the series, where one name typed releases every volume — not to
    /// the first of forty volumes, each asking the same question.
    void next_leads_to_the_series_when_its_volumes_lack_a_writer()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aSeriesSaying(u"Sans scénariste"_s,
                                        R"j(,"readingDirection":"LEFT_TO_RIGHT","artists":["Nesmo"])j")});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 1, 30000);
        m_review->view()->select(u"all"_s);

        QVERIFY(m_review->view()->showBlocked());
        QCOMPARE(m_review->sheet().value(u"id"_s).toString(), idNamed(u"Sans scénariste"_s));
    }

    /// Where a value was read — « Écrit », « Repris », « Deviné » — is not shown: the field
    /// shows what it holds, and red only when it is required and nobody said it.
    void a_field_shows_no_origin()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aCompleteSeries(u"Prête"_s)});
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->open();
        m_review->view()->select(idNamed(u"Prête"_s));

        const QVariantMap title = fieldOnSheet(u"title"_s);
        QVERIFY(!title.isEmpty());
        QVERIFY(!title.contains(u"originLabel"_s));
        QVERIFY(!title.contains(u"origin"_s));
        QCOMPARE(title.value(u"lacking"_s).toBool(), false);
    }

    /// A facultative field nobody filled in is nothing to look at.
    void an_optional_field_left_empty_is_not_red()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aCompleteSeries(u"Prête"_s)});
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->open();
        m_review->view()->select(idNamed(u"Prête"_s));

        const QVariantMap publisher = fieldOnSheet(u"publisher"_s);
        QVERIFY(!publisher.isEmpty());
        QCOMPARE(publisher.value(u"text"_s).toString(), QString());
        QVERIFY(publisher.contains(u"lacking"_s));
        QCOMPARE(publisher.value(u"lacking"_s).toBool(), false);
    }

    /// The reading direction offers its three choices on the sheet of the series that lacks
    /// it, and choosing one is what releases it.
    void a_series_with_no_direction_offers_it_and_choosing_releases_it()
    {
        m_pretend->answerFor = sending();
        m_offers->offer({aSeriesSaying(u"Sans sens"_s,
                                        R"j(,"authors":["Jarry"],"artists":["Nesmo"])j")});
        QTRY_COMPARE_WITH_TIMEOUT(m_tally->blocked(), 1, 30000);
        m_review->view()->open();

        QCOMPARE(fieldOnSheet(u"readingDirection"_s).value(u"choices"_s).toList().size(), 3);
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' ' + Words::reviewFooter(1, 0));
        QVERIFY(m_review->edits()->edit(idOf(0, {}), u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QTRY_COMPARE(m_tally->blocked(), 0);
    }

    /// What its volumes say is said for the series: one that names a writer and a drawer in
    /// its volumes asks nothing of the series, and the footer does not count them.
    void the_writer_and_the_drawer_of_the_volumes_are_not_missing_on_the_series()
    {
        m_pretend->answerFor = sending();
        const QString at = m_folder.filePath(u"Volumes dits"_s);
        QVERIFY(QDir().mkpath(at));
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"title":"Volumes dits","readingDirection":"LEFT_TO_RIGHT"})");
        declaration.close();
        QFile volume(at + u"/Tome 1.cbz"_s);
        QVERIFY(volume.open(QIODevice::WriteOnly));
        volume.write(Zip::volume(R"({"leaf":1,"number":1,"authors":["Jarry"],"artists":["Nesmo"]})"));
        volume.close();
        m_offers->offer({at});
        QTRY_COMPARE(readyCards(), 1);
        m_review->view()->open();

        QCOMPARE(m_tally->blocked(), 0);
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
    }

    /// One series dropped alone opens whole, under « Sans univers ».
    void a_lone_series_opens_unfolded()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->toggle(u"loose"_s);
        m_review->view()->open();
        QCOMPARE(railLine(u"loose"_s).value(u"expanded"_s).toBool(), true);
        QVERIFY(!railLine(idOf(0, {})).isEmpty());
    }

    /// While series are ticked, a click on one of their lines shows its sheet beside them:
    /// moving into the series instead forgot every tick for a look at one.
    void a_click_on_a_line_shows_its_sheet_and_keeps_the_ticks()
    {
        komgaSeriesListed(3);
        m_review->ticks()->tick(idOf(0, {}), true);
        m_review->ticks()->tick(idOf(1, {}), true);
        QVERIFY(m_review->ticks()->groupedShown());

        m_review->view()->select(idOf(2, {}));
        QCOMPARE(m_review->ticks()->ticked(), 2);
        QVERIFY(!m_review->ticks()->groupedShown());
        QCOMPARE(m_review->sheet().value(u"id"_s).toString(), idOf(2, {}));
        QCOMPARE(lines().size(), 3);

        // Ticking again shows the grouped values again.
        m_review->ticks()->tick(idOf(2, {}), true);
        QVERIFY(m_review->ticks()->groupedShown());
        QCOMPARE(m_review->ticks()->ticked(), 3);
    }

    /// A universe declared inside a declared universe is refused before any walk, and its
    /// card had no sheet: the frame showed nothing of it. It is listed with its reason, as
    /// what it declares — a universe, not a series — and can be taken away.
    void a_declaration_out_of_place_is_listed_with_its_reason_and_can_be_removed()
    {
        m_pretend->answerFor = sessions();
        const QString monde = m_folder.filePath(u"Monde"_s);
        const QString arran = monde + u"/Terres d’Arran"_s;
        for (const QString &at : {monde, arran}) {
            QVERIFY(QDir().mkpath(at));
            QFile declaration(at + u"/universe.json"_s);
            QVERIFY(declaration.open(QIODevice::WriteOnly));
            declaration.write(R"({"leaf":1,"name":")" + QFileInfo(at).fileName().toUtf8()
                              + R"("})");
        }
        QVERIFY(QDir().mkpath(arran + u"/Elfes"_s));
        aDeclaration(arran + u"/Elfes"_s, "Elfes");
        aVolume(arran + u"/Elfes/Tome 1.cbz"_s, 1);
        m_offers->offer({monde});
        QCOMPARE(stageOf(0), int(Imports::Stage::Failed));
        const QString why = m_imports->cards().at(0).trouble;
        QCOMPARE(why, Words::declaredOutOfPlace(u"Terres d’Arran"_s, u"universe.json"_s,
                                                Words::impossibleUniverseInUniverse()));
        m_review->view()->select(u"all"_s);

        QCOMPARE(lines().size(), 1);
        const QVariantMap line = lines().constFirst();
        QCOMPARE(line.value(u"chip"_s).toString(), Words::failedChip());
        QCOMPARE(line.value(u"detail"_s).toString(), why);
        QCOMPARE(line.value(u"name"_s).toString(), u"Monde"_s);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 1);
        const QVariantList rail = m_review->rail();
        const auto node = std::ranges::find_if(rail, [this](const QVariant &one) {
            return one.toMap().value(u"id"_s).toString() == idOf(0, {});
        });
        QVERIFY(node != rail.cend());
        QCOMPARE(node->toMap().value(u"role"_s).toInt(), int(Shape::Role::Universe));

        m_review->view()->open();
        const QVariantMap sheet = m_review->sheet();
        QCOMPARE(sheet.value(u"id"_s).toString(), idOf(0, {}));
        QCOMPARE(sheet.value(u"removable"_s).toBool(), true);
        QCOMPARE(sheet.value(u"editable"_s).toBool(), false);
        QCOMPARE(sheet.value(u"notes"_s).toList().constFirst().toMap().value(u"text"_s).toString(),
                 why);
        QVERIFY(m_review->edits()->remove(idOf(0, {})));
        QCOMPARE(m_imports->rowCount({}), 0);
    }

    /// A card « Importer » has let go waits its turn behind the one in flight, and is not
    /// corrected any more: corrected, it went back to « À vérifier » under a frame opened
    /// on the transfers, where nothing offers to import it again.
    void a_card_already_let_go_cannot_be_edited_while_it_waits()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            const QStringList three{u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s};
            if (request.startsWith("POST /import ") && request.contains("\"root\":\"Koro\""))
                return anOpening("Koro", three, {});
            if (request.startsWith("POST /import "))
                return anOpening("Mira", three, {});
            return QByteArray();  // the transfer never answers: it stays in flight
        };
        m_offers->offer({aCompleteSeries(), aCompleteSeries(u"Mira"_s)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_queue->send();
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Sending));
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));

        m_review->view()->select(idOf(1, {}));
        QVERIFY(!m_review->sheet().value(u"editable"_s).toBool());
        QVERIFY(!m_review->edits()->edit(idOf(1, {}), u"title"_s, u"Autre"_s));
        QCOMPARE(stageOf(1), int(Imports::Stage::Ready));
        // And it is not counted as going at « Importer » once more: it already goes.
        QVERIFY(!m_review->importable());
    }

    /// A declaration that differs from the library's is written only once its box is
    /// ticked, and the boxes start empty: unticked, the series held whole stops nothing and
    /// the frame folds it with the identical ones — it stopped at « À vérifier » over a
    /// `work.json` that was never going to be written. Ticked, it travels.
    void a_differing_declaration_travels_only_once_ticked()
    {
        aHeldSeriesAnnounced(R"("creates":[],"declarations":[{"path":"work.json","present":{},)"
                             R"("presentName":"Blue Period","differs":["title"]}])");
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->select(u"all"_s);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 1);
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 0);
        QVERIFY(m_review->nothingToImport());
        const Card &card = m_imports->cards().at(0);
        QVERIFY(!Carrier::travels(card, card.road.sheets.constFirst()));

        m_imports->setDeclaring(0, u"work.json"_s, true);
        const Card &ticked = m_imports->cards().at(0);
        QVERIFY(Carrier::travels(ticked, ticked.road.sheets.constFirst()));
        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 1);
        QVERIFY(!m_review->nothingToImport());
    }

    /// An edition created under a series the library holds whole writes the edition's
    /// file, not the series': the series' sheet does not travel, and stops nothing.
    void an_edition_created_under_a_held_series_is_not_the_series()
    {
        const QString at = m_folder.filePath(u"Dragon"_s);
        QVERIFY(QDir().mkpath(at + u"/Perfect"_s));
        QFile edition(at + u"/Perfect/edition.json"_s);
        QVERIFY(edition.open(QIODevice::WriteOnly));
        edition.write(R"({"leaf":1,"name":"Perfect","readingDirection":"RIGHT_TO_LEFT"})");
        edition.close();
        // Declared, so that the card is the series and the edition one of its folders.
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write(R"({"leaf":1,"title":"Dragon"})");
        declaration.close();
        aVolume(at + u"/Perfect/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Dragon", {}, {u"Perfect/Tome 1.cbz"_s},
                                 R"([{"kind":"EDITION","name":"Perfect","at":"Perfect"}])");
            return QByteArray();
        };
        m_offers->offer({at});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));

        const Card &card = m_imports->cards().at(0);
        const auto sheet = [&card](const QString &where) {
            return *std::ranges::find(card.road.sheets, where, &Facts::Sheet::at);
        };
        QVERIFY(!Carrier::travels(card, sheet({})));
        QVERIFY(Carrier::travels(card, sheet(u"Perfect"_s)));
        QCOMPARE(stageOf(0), int(Imports::Stage::Ready));
        // And the series, holding something that goes, is no « rien à faire ».
        m_review->view()->select(u"all"_s);
        QCOMPARE(countOf(Review::Filter::Identical).value(u"count"_s).toInt(), 0);
    }

    /// A series the library holds whole whose `work.json` will still be written is held
    /// back by what holds that file back, at « Importer » as in the frame: the frame said
    /// « nothing to import » for it while « Importer » let it go.
    void a_held_series_whose_declaration_is_unreadable_is_held_back_everywhere()
    {
        const QString at = aKomgaSeries();
        QFile declaration(at + u"/work.json"_s);
        QVERIFY(declaration.open(QIODevice::WriteOnly));
        declaration.write("[1, 2]");
        declaration.close();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_h","root":"Blue Period","toSend":[],)"
                               R"("alreadyThere":)" + listed(komgaNames(1, 14))
                                   + R"(,"bytesToSend":0,"creates":)"
                                     R"([{"kind":"WORK","name":"Blue Period","at":""}]})");
        };
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        QVERIFY(!m_review->importable());

        m_queue->confirm();
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// The value a field already holds, typed again, is no correction: no « edited » mark,
    /// no new announcement, no archive rewritten for nothing.
    void typing_the_value_already_there_corrects_nothing()
    {
        aKomgaSeriesAnnounced();
        m_review->view()->open();

        QVERIFY(!m_review->edits()->edit(idOf(0, {}), u"title"_s, u"Blue Period"_s));
        const Facts::Field title = fieldOnCard(0, {}, u"title"_s);
        QVERIFY(!title.edited);
        QCOMPARE(title.origin, Facts::Origin::Taken);
        QVERIFY(!m_imports->cards().at(0).road.edited);
        QCOMPARE(stageOf(0), int(Imports::Stage::Reviewing));
    }

    /// A ComicInfo that opens and says nothing worth taking is not « illisible »: only one
    /// that could not be read is.
    void a_comic_info_that_says_nothing_is_not_called_unreadable()
    {
        const QString at = m_folder.filePath(u"Quiet"_s);
        QVERIFY(QDir().mkpath(at));
        aDeclaration(at, "Quiet");
        QFile file(at + u"/Tome 1.cbz"_s);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(Zip::volume(R"({"leaf":1,"number":1})",
                               "<ComicInfo><Pages><Page Image=\"0\"/></Pages></ComicInfo>"));
        file.close();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /import "))
                return anOpening("Quiet", {u"Tome 1.cbz"_s}, {});
            return QByteArray();
        };
        m_offers->offer({at});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        m_review->view()->select(idOf(0, u"Tome 1.cbz"_s));

        const QVariantList notes = m_review->sheet().value(u"notes"_s).toList();
        QVERIFY(std::ranges::none_of(notes, [](const QVariant &one) {
            return one.toMap().value(u"text"_s).toString() == Words::comicInfoUnreadable();
        }));
    }

    /// A shelf whose folders say nothing opens on the shelf, its three readings listed and
    /// the first one to confirm on the sheet, and the frame says that nothing was said: read
    /// as « 1 univers · 4 séries », the line above counted guesses as if they were facts.
    void a_drop_without_information_opens_on_the_shelf_and_says_how_it_was_read()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        m_review->view()->open();

        const QString shelf = u"drop/%1/"_s.arg(m_imports->cards().at(0).road.drop);
        QCOMPARE(railNamed(u"BD"_s).value(u"id"_s).toString(), shelf);
        QVERIFY(railNamed(u"BD"_s).value(u"selected"_s).toBool());
        QCOMPARE(m_review->listTitle(), Words::insideOf(u"BD"_s));
        QCOMPARE(m_review->sheet().value(u"title"_s).toString(), u"Dragon Ball"_s);
        QCOMPARE(m_review->railTitle(), Words::proposedApport());
        QCOMPARE(m_review->railHint(), Words::roleHint());
        QCOMPARE(m_review->railNote(), Words::nothingSaid());
        QCOMPARE(m_review->subtitle(), Words::dropSummary(1, 15, Words::Source::Nothing));

        QCOMPARE(lines().size(), 3);
        const QVariantMap terres = lineNamed(u"Terres d'Arran"_s);
        QCOMPARE(terres.value(u"chip"_s).toString(),
                 Words::readingChip(Shape::Role::Universe, Shape::Confidence::Likely, false));
        QCOMPARE(terres.value(u"origin"_s).toInt(), int(Review::Origin::Guessed));
        QCOMPARE(terres.value(u"detail"_s).toString(),
                 Words::foldersHeld({{u"Elfes"_s, 4}, {u"Mages"_s, 3}}, 0));
        const QVariantMap dragon = lineNamed(u"Dragon Ball"_s);
        QCOMPARE(dragon.value(u"chip"_s).toString(),
                 Words::readingChip(Shape::Role::Work, Shape::Confidence::ToConfirm, false));
        QCOMPARE(dragon.value(u"origin"_s).toInt(), int(Review::Origin::Missing));
        const QVariantMap vrac = lineNamed(u"Vrac"_s);
        QCOMPARE(vrac.value(u"chip"_s).toString(),
                 Words::readingChip(Shape::Role::Work, Shape::Confidence::Certain, true));
        QCOMPARE(vrac.value(u"origin"_s).toInt(), int(Review::Origin::Missing));
        QCOMPARE(vrac.value(u"detail"_s).toString(), Words::foldersHeld({}, 2));
    }

    /// Every folder of the rail says what it was read as and how surely, the shelf above its
    /// cards included: a role guessed « à confirmer » drawn like one a sidecar says is the
    /// one guess nobody looks at twice.
    void each_folder_of_the_rail_carries_its_role_and_how_sure_it_is()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);

        const QString shelf = u"drop/%1/"_s.arg(m_imports->cards().at(0).road.drop);
        QCOMPARE(railIds(),
                 (QStringList{u"all"_s, shelf, idNamed(u"Dragon Ball"_s),
                              idNamed(u"Dragon Ball"_s, u"Original"_s),
                              idNamed(u"Dragon Ball"_s, u"Perfect"_s),
                              idNamed(u"Terres d'Arran"_s),
                              idNamed(u"Terres d'Arran"_s, u"Elfes"_s),
                              idNamed(u"Terres d'Arran"_s, u"Mages"_s), idNamed(u"Vrac"_s)}));
        const auto says = [this](const QString &name, Shape::Role role, Review::Origin tone,
                                 int depth) {
            const QVariantMap node = railNamed(name);
            QCOMPARE(node.value(u"role"_s).toInt(), int(role));
            QCOMPARE(node.value(u"roleLabel"_s).toString(), Words::role(role));
            QCOMPARE(node.value(u"roleTone"_s).toInt(), int(tone));
            QCOMPARE(node.value(u"depth"_s).toInt(), depth);
        };
        // The shelf is certain — a universe is inside it — and asks nothing.
        says(u"BD"_s, Shape::Role::Shelf, Review::Origin::Guessed, 1);
        QCOMPARE(railNamed(u"BD"_s).value(u"level"_s).toInt(), -1);
        says(u"Dragon Ball"_s, Shape::Role::Work, Review::Origin::Missing, 2);
        // An edition read from its series' reading is as sure as that reading.
        says(u"Perfect"_s, Shape::Role::Edition, Review::Origin::Missing, 3);
        says(u"Terres d'Arran"_s, Shape::Role::Universe, Review::Origin::Guessed, 2);
        says(u"Elfes"_s, Shape::Role::Work, Review::Origin::Guessed, 3);
        says(u"Vrac"_s, Shape::Role::Work, Review::Origin::Missing, 2);
        QCOMPARE(railNamed(Words::wholeApport()).value(u"role"_s).toInt(), -1);
    }

    /// The sheet of a folder read two ways shows both, each with what it makes of the drop,
    /// the one in force marked, and why the first: a choice offered without its counts is a
    /// choice made blind.
    void the_sheet_of_a_folder_offers_two_readings_with_their_counts_and_clues()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        m_review->view()->select(idNamed(u"Dragon Ball"_s));

        const QList<QVariantMap> readings = listOn(u"readings"_s);
        QCOMPARE(readings.size(), 2);
        QCOMPARE(readings.at(0).value(u"label"_s).toString(),
                 Words::readingName(Shape::Role::Work, 0, 1, 2));
        QCOMPARE(readings.at(0).value(u"summary"_s).toString(), Words::readingSummary(0, 1, 2, 6));
        QVERIFY(readings.at(0).value(u"chosen"_s).toBool());
        QCOMPARE(readings.at(0).value(u"badge"_s).toString(), Words::mostLikely());
        QCOMPARE(readings.at(0).value(u"badgeTone"_s).toInt(), int(Review::Origin::Guessed));
        QCOMPARE(readings.at(1).value(u"label"_s).toString(),
                 Words::readingName(Shape::Role::Universe, 1, 2, 0));
        QCOMPARE(readings.at(1).value(u"summary"_s).toString(), Words::readingSummary(1, 2, 0, 6));
        QVERIFY(!readings.at(1).value(u"chosen"_s).toBool());
        QCOMPARE(readings.at(1).value(u"badge"_s).toString(), QString());

        QCOMPARE(m_review->sheet().value(u"cluesTitle"_s).toString(), Words::whyTheFirst());
        const QList<QVariantMap> clues = listOn(u"clues"_s);
        QCOMPARE(clues.size(), 3);
        QCOMPARE(clues.at(0).value(u"text"_s).toString(),
                 Words::clueEditionWords({u"Original"_s, u"Perfect"_s}));
        QVERIFY(clues.at(0).value(u"forIt"_s).toBool());
        QCOMPARE(clues.at(1).value(u"text"_s).toString(), Words::clueSameNumbers(2, 1, 3));
        QCOMPARE(clues.at(2).value(u"text"_s).toString(), Words::clueProperNames());
        QVERIFY(!clues.at(2).value(u"forIt"_s).toBool());

        // A folder that holds its archives has one reading, and nothing to prefer it over.
        m_review->view()->select(idNamed(u"Vrac"_s));
        QCOMPARE(listOn(u"readings"_s).size(), 1);
        QCOMPARE(listOn(u"readings"_s).at(0).value(u"badge"_s).toString(), Words::nameToGive());
        // A name to give is a fault to mend, drawn as one: the badge of a guess beside it
        // said nothing was missing.
        QCOMPARE(listOn(u"readings"_s).at(0).value(u"badgeTone"_s).toInt(),
                 int(Review::Origin::Missing));
        QCOMPARE(m_review->sheet().value(u"cluesTitle"_s).toString(), Words::why());
    }

    /// The other reading is one click, and the rail follows it at once: Perfect and Original
    /// are series of a universe, no longer editions — and no longer asked about, since a
    /// reading chosen is somebody's.
    void choosing_the_other_reading_rearranges_the_rail()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const QString dragon = idNamed(u"Dragon Ball"_s);
        QCOMPARE(railNamed(u"Perfect"_s).value(u"roleLabel"_s).toString(),
                 Words::role(Shape::Role::Edition));

        QVERIFY(!m_review->edits()->chooseReading(dragon, 2));
        QVERIFY(m_review->edits()->chooseReading(dragon, 1));
        QCOMPARE(railNamed(u"Dragon Ball"_s).value(u"roleLabel"_s).toString(),
                 Words::role(Shape::Role::Universe));
        for (const QString &name : {u"Perfect"_s, u"Original"_s}) {
            QCOMPARE(railNamed(name).value(u"roleLabel"_s).toString(),
                     Words::role(Shape::Role::Work));
            QCOMPARE(railNamed(name).value(u"roleTone"_s).toInt(), int(Review::Origin::Guessed));
        }
        m_review->view()->select(dragon);
        QVERIFY(!listOn(u"readings"_s).at(0).value(u"chosen"_s).toBool());
        QVERIFY(listOn(u"readings"_s).at(1).value(u"chosen"_s).toBool());
    }

    /// The menu of a role lists every role, each with what it is or why it is greyed here —
    /// a role greyed without its reason reads as a menu that is broken. « Édition » is greyed
    /// too: making Vrac the edition of a neighbour means moving it, which no click here does.
    void the_role_menu_lists_every_role_with_its_reason()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const QString vrac = idNamed(u"Vrac"_s);

        const QVariantList options = m_review->edits()->roleOptions(vrac);
        QCOMPARE(options.size(), 5);
        const QList<std::tuple<Shape::Role, bool, QString>> expected{
            {Shape::Role::Work, true, Words::roleDetail(Shape::Role::Work)},
            {Shape::Role::Edition, false, Words::impossibleEditionAlone()},
            {Shape::Role::Universe, false, Words::impossibleNoSubfolder()},
            {Shape::Role::Shelf, false, Words::impossibleHoldsFiles()},
            {Shape::Role::Book, false, Words::impossibleBookIsAnArchive()},
        };
        for (qsizetype at = 0; at < expected.size(); ++at) {
            const QVariantMap one = options.at(at).toMap();
            const auto &[role, possible, detail] = expected.at(at);
            QCOMPARE(one.value(u"role"_s).toInt(), int(role));
            QCOMPARE(one.value(u"label"_s).toString(), Words::roleOption(role));
            QCOMPARE(one.value(u"possible"_s).toBool(), possible);
            QCOMPARE(one.value(u"detail"_s).toString(), detail);
            QCOMPARE(one.value(u"chosen"_s).toBool(), role == Shape::Role::Work);
        }
        QVERIFY(!m_review->edits()->setRole(vrac, int(Shape::Role::Universe)));
        QVERIFY(!m_review->edits()->setRole(vrac, 42));

        // The shelf has a menu of its own: it holds a universe, so it can be nothing else.
        const QString shelf = u"drop/%1/"_s.arg(m_imports->cards().at(0).road.drop);
        const QVariantList onShelf = m_review->edits()->roleOptions(shelf);
        QCOMPARE(onShelf.size(), 5);
        QVERIFY(std::ranges::any_of(onShelf, [](const QVariant &one) {
            return one.toMap().value(u"role"_s).toInt() == int(Shape::Role::Shelf)
                   && one.toMap().value(u"chosen"_s).toBool();
        }));
    }

    /// What a reading writes is counted by file name, over the card: a sheet that said
    /// « 87 fichiers » would not say that 84 of them are the archives' own `entry.json`.
    void what_will_be_written_is_counted_by_file_name()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders(42)});
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), 3, 30000);
        m_review->view()->select(idNamed(u"Dragon Ball"_s));

        const QList<QVariantMap> writes = listOn(u"writes"_s);
        QCOMPARE(writes.size(), 3);
        const QList<std::pair<QString, int>> expected{
            {u"work.json"_s, 1}, {u"edition.json"_s, 2}, {u"entry.json"_s, 84}};
        for (qsizetype at = 0; at < expected.size(); ++at) {
            QCOMPARE(writes.at(at).value(u"name"_s).toString(), expected.at(at).first);
            QCOMPARE(writes.at(at).value(u"count"_s).toInt(), expected.at(at).second);
            QCOMPARE(writes.at(at).value(u"text"_s).toString(),
                     Words::writtenTimes(expected.at(at).first, expected.at(at).second));
        }
        QCOMPARE(m_review->sheet().value(u"writesNote"_s).toString(), Words::writesNote());
    }

    /// The archives at a universe's root come first, in a group of their own, and the one
    /// that declares nothing says it will not be read: the scan puts it in `disregarded`, and
    /// a line drawn like the others would let it vanish from the library without a word.
    void an_archive_at_a_universe_root_is_grouped_first_and_said_unread()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithArchivesAtItsRoot(true)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QString scan = idOf(0, u"scan_final.cbz"_s);
        const QString book = idOf(0, u"le_combat.cbz"_s);

        QCOMPARE(m_review->subtitle(), Words::rootSummary(1, 1, 2));
        QCOMPARE(m_review->railNote(), Words::onlyABookAtTheRoot());
        QCOMPARE(m_review->railTitle(), Words::proposedApport());
        QCOMPARE(railIds(), (QStringList{u"all"_s, idOf(0, {}), book, scan, idOf(0, u"Elfes"_s)}));
        QCOMPARE(railNamed(u"Le Combat"_s).value(u"role"_s).toInt(), int(Shape::Role::Book));
        QCOMPARE(railNamed(u"Le Combat"_s).value(u"roleTone"_s).toInt(),
                 int(Review::Origin::Said));
        QCOMPARE(railNamed(u"scan_final.cbz"_s).value(u"role"_s).toInt(),
                 int(Shape::Role::Unread));
        QCOMPARE(railNamed(u"scan_final.cbz"_s).value(u"roleTone"_s).toInt(),
                 int(Review::Origin::Missing));

        m_review->view()->select(idOf(0, {}));
        QCOMPARE(headers().constFirst(), Words::atTheRoot(2));
        QCOMPARE(lines().at(0).value(u"id"_s).toString(), book);
        QCOMPARE(lines().at(1).value(u"id"_s).toString(), scan);
        QCOMPARE(lines().at(1).value(u"chip"_s).toString(), Words::role(Shape::Role::Unread));
        QCOMPARE(lines().at(1).value(u"origin"_s).toInt(), int(Review::Origin::Missing));
        QCOMPARE(lines().at(1).value(u"detail"_s).toString(), Words::noRoleNotRead());
        QCOMPARE(lines().at(2).value(u"name"_s).toString(), u"Elfes"_s);
        QCOMPARE(countOf(Review::Filter::All).value(u"count"_s).toInt(), 3);

        m_review->view()->select(scan);
        QVERIFY(sheetSays(Words::unreadAtTheRoot(), true));
        const QList<QVariantMap> place = listOn(u"place"_s);
        QCOMPARE(place.size(), 2);
        QCOMPARE(place.at(0).value(u"role"_s).toInt(), int(Shape::Role::Book));
        QCOMPARE(place.at(0).value(u"label"_s).toString(), Words::declareABook());
        QVERIFY(place.at(0).value(u"possible"_s).toBool());
        QVERIFY(!place.at(0).value(u"chosen"_s).toBool());
        QCOMPARE(place.at(1).value(u"role"_s).toInt(), int(Shape::Role::Volume));
        QCOMPARE(place.at(1).value(u"label"_s).toString(), Words::fileInASeries());
        QVERIFY(!place.at(1).value(u"possible"_s).toBool());
        QCOMPARE(place.at(1).value(u"detail"_s).toString(), Words::impossibleFilingMoves());

        QVERIFY(m_review->edits()->setRole(scan, int(Shape::Role::Book)));
        QCOMPARE(railNamed(u"scan_final"_s).value(u"role"_s).toInt(), int(Shape::Role::Book));
        QVERIFY(listOn(u"place"_s).at(0).value(u"chosen"_s).toBool());
        QVERIFY(!sheetSays(Words::unreadAtTheRoot(), true));
    }

    /// An archive whose `oneshot.json` does not read is shown as the server reads it, no
    /// book, says so, and cannot be made one: a new `oneshot.json` would be written over one
    /// nobody read.
    void an_unreadable_oneshot_greys_the_book_role()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithArchivesAtItsRoot(false, "[1, 2]")});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QString scan = idOf(0, u"scan_final.cbz"_s);
        m_review->view()->select(scan);

        QVERIFY(sheetSays(Words::oneShotUnreadable(), false));
        const QVariantList options = m_review->edits()->roleOptions(scan);
        const auto book = std::ranges::find_if(options, [](const QVariant &one) {
            return one.toMap().value(u"role"_s).toInt() == int(Shape::Role::Book);
        });
        QVERIFY(book != options.end());
        QVERIFY(!book->toMap().value(u"possible"_s).toBool());
        QCOMPARE(book->toMap().value(u"detail"_s).toString(), Words::oneShotUnreadableReason());
        QVERIFY(!listOn(u"place"_s).at(0).value(u"possible"_s).toBool());
        QCOMPARE(listOn(u"place"_s).at(0).value(u"detail"_s).toString(),
                 Words::oneShotUnreadableReason());

        QVERIFY(!m_review->edits()->setRole(scan, int(Shape::Role::Book)));
        QCOMPARE(railNamed(u"scan_final.cbz"_s).value(u"role"_s).toInt(),
                 int(Shape::Role::Unread));
    }

    /// While a reading is left, the button confirms them and the footer says nothing goes
    /// before; an archive no one will read is counted first, and never in « Importer N
    /// tomes » — it is not announced, and the button said it would send it.
    void the_footer_asks_to_confirm_the_readings_and_counts_what_is_left_out()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithArchivesAtItsRoot(false)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));

        QCOMPARE(m_review->importLabel(), Words::confirmReadings(1));
        QCOMPARE(m_review->footer(),
                 Words::afterLeftOut(1, Words::nothingSentBeforeReadings(1)));

        QVERIFY(m_review->edits()->setRole(idOf(0, u"scan_final.cbz"_s), int(Shape::Role::Unread)));
        QCOMPARE(m_imports->readings(), 0);
        QCOMPARE(m_review->importLabel(), Words::importVolumes(1));
        QCOMPARE(m_review->footer(), Words::afterLeftOut(1, Words::reviewFooter(0, 0)));

        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 4);
        QCOMPARE(m_review->importLabel(), Words::confirmReadings(3));
        // The three series wait — « Vrac » for its title still to give, every one for who wrote
        // and drew it and which way it reads — and are said to.
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(3, 0, 0) + u' '
                     + Words::afterLeftOut(1, Words::nothingSentBeforeReadings(3)));
    }

    /// A drop that says everything about itself has nothing proposed in it: the caption is
    /// the plain one, no hint about labels, and the button and the footer count volumes and
    /// values — a « rôle proposé » over folders whose sidecars say their role would ask a
    /// question nobody needs to answer.
    void a_drop_said_in_full_keeps_its_ordinary_frame()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            if (request.contains("\"root\":\"Terres\""))
                return anOpening("Terres", {u"Elfes/Tome 1.cbz"_s, u"Mages/Tome 1.cbz"_s}, {});
            return anOpening("Koro", {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s}, {});
        };
        m_offers->offer({aUniverse(), aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_review->view()->open();

        QCOMPARE(m_review->railTitle(), Words::apport());
        QCOMPARE(m_review->railHint(), QString());
        QCOMPARE(m_review->railNote(), Words::everythingSaid());
        QCOMPARE(m_review->subtitle(), Words::apportSummary(1, 3, 5, Words::Source::Leaf));
        QCOMPARE(m_review->importLabel(), Words::importVolumes(5));
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
        // A universe beside « Sans univers » opens folded, as each of two nodes does; opened,
        // the tree holds what it always held.
        QCOMPARE(railIds(), (QStringList{u"all"_s, idOf(0, {}), u"loose"_s}));
        m_review->view()->toggle(idOf(0, {}));
        m_review->view()->toggle(u"loose"_s);
        QCOMPARE(railIds(), (QStringList{u"all"_s, idOf(0, {}), idOf(0, u"Elfes"_s),
                                         idOf(0, u"Mages"_s), u"loose"_s, idOf(1, {})}));
        QCOMPARE(railNamed(u"Terres d’Arran"_s).value(u"roleTone"_s).toInt(),
                 int(Review::Origin::Said));
        QCOMPARE(railNamed(u"Koro Quest"_s).value(u"roleLabel"_s).toString(),
                 Words::role(Shape::Role::Work));
    }

    /// Drawing a shelf of fifty universes, each asking its reading, and saying everything
    /// around it — the list, the line under the title, the caption, the note, the footer and
    /// the button — looks at each role a bounded number of times and asks whether anything
    /// is proposed once: a rail that looked for each card's shelf by walking every card, or
    /// a caption that walked the whole drop again for each property, costs fifty times
    /// fifty on every change of the queue.
    void the_frame_reads_each_role_a_few_times_whatever_the_roles()
    {
        constexpr int Worlds = 50;
        const QString rayon = m_folder.filePath(u"Rayon"_s);
        // Two series numbered apart in each: a universe, « probable », whose reading is asked.
        for (int one = 0; one < Worlds; ++one) {
            const QString world = rayon + u"/Monde %1"_s.arg(one);
            bareVolumes(world + u"/Est %1"_s.arg(one), 4 * one + 1, 4 * one + 2);
            bareVolumes(world + u"/Ouest %1"_s.arg(one), 4 * one + 3, 4 * one + 4);
        }
        m_pretend->answerFor = sessions();
        m_offers->offer({rayon});
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), Worlds, 60000);
        qint64 entries = 0;
        qint64 sheets = 0;
        for (const Card &card : m_imports->cards()) {
            entries += card.road.tree.files.size();
            sheets += card.road.sheets.size();
        }
        // Something changed, so nothing of the frame is reused from before it.
        emit m_imports->changed();

        const Review::Cost before = Review::cost();
        const QVariantList rail = m_review->rail();
        m_review->view()->select(rail.at(1).toMap().value(u"id"_s).toString());
        const QList<QVariantMap> listed = lines();
        const QString subtitle = m_review->subtitle();
        const QString title = m_review->railTitle();
        const QString hint = m_review->railHint();
        const QString note = m_review->railNote();
        const QString footer = m_review->footer();
        const QString label = m_review->importLabel();
        const Review::Cost after = Review::cost();

        QCOMPARE(listed.size(), Worlds);
        QCOMPARE(listed.constFirst().value(u"chip"_s).toString(),
                 Words::readingChip(Shape::Role::Universe, Shape::Confidence::Likely, false));
        QCOMPARE(rail.at(1).toMap().value(u"role"_s).toInt(), int(Shape::Role::Shelf));
        QCOMPARE(title, Words::proposedApport());
        QCOMPARE(hint, Words::roleHint());
        QCOMPARE(subtitle, Words::dropSummary(1, int(entries), Words::Source::Nothing));
        QCOMPARE(label, Words::confirmReadings(Worlds));
        QVERIFY(!note.isEmpty() && !footer.isEmpty());
        QVERIFY2(after.entries - before.entries <= entries,
                 qPrintable(QString::number(after.entries - before.entries)));
        QVERIFY2(after.looks - before.looks <= 4 * sheets,
                 qPrintable(u"%1 looks for %2 sheets"_s.arg(after.looks - before.looks)
                                .arg(sheets)));
        QCOMPARE(after.proposals - before.proposals, 1);

        // And the tree alone says, line by line, whether another role can be chosen there,
        // a bounded number of looks a line: asked by each chip of the screen instead, a
        // shelf of fifty asked for fifty menus on every rebuild, and nothing counted them.
        const Review::Cost bare = Review::cost();
        const QVariantList again = m_review->rail();
        const qint64 looks = Review::cost().looks - bare.looks;
        QVERIFY2(looks <= 3 * again.size(),
                 qPrintable(u"%1 looks for %2 lines"_s.arg(looks).arg(again.size())));
        for (const QVariant &one : again) {
            const QVariantMap node = one.toMap();
            QVERIFY2(node.contains(u"changeable"_s), qPrintable(node.value(u"name"_s).toString()));
        }
    }

    /// The tree of `shelves` shelves of `perShelf` series each — one shelf holding them
    /// all when `shelves` is 1 — built, and what it cost, line by line.
    void aThousandSeriesCostAFewLooksALine(int shelves, int perShelf)
    {
        const QString bd = m_folder.filePath(u"BD"_s);
        for (int shelf = 0; shelf < shelves; ++shelf) {
            const QString at = shelves == 1 ? bd : bd + u"/Rayon %1"_s.arg(shelf);
            for (int one = 0; one < perShelf; ++one)
                bareVolumes(at + u"/Série %1-%2"_s.arg(shelf).arg(one), 1, 1);
        }
        m_pretend->answerFor = sessions();
        m_offers->offer({bd});
        QTRY_COMPARE_WITH_TIMEOUT(m_imports->reviewing(), shelves * perShelf, 180000);
        emit m_imports->changed();

        const Review::Cost review = Review::cost();
        const Shape::Cost shape = Shape::cost();
        const Imports::Cost imports = Imports::cost();
        const QVariantList rail = m_review->rail();
        const qint64 lines = rail.size();
        QVERIFY(lines >= shelves * perShelf);
        // Every card can still be corrected, so no role is tried on a copy of the drop: one
        // per role offered on a shelf, at every rebuild, compared every card of the drop.
        QCOMPARE(Imports::cost().trials - imports.trials, 0);
        const qint64 looks = Review::cost().looks - review.looks;
        QVERIFY2(looks <= 6 * lines, qPrintable(u"%1 looks for %2 lines"_s.arg(looks).arg(lines)));
        const qint64 found = Shape::cost().looks - shape.looks;
        QVERIFY2(found <= 8 * lines,
                 qPrintable(u"%1 looks in the drop for %2 lines"_s.arg(found).arg(lines)));
    }

    /// A thousand series on one shelf: the shelf's line asked whether each of its roles
    /// could be taken by copying the whole drop and comparing every card, at every rebuild.
    void a_thousand_series_on_one_shelf_cost_a_few_looks_a_line()
    {
        aThousandSeriesCostAFewLooksALine(1, 1000);
    }

    /// And on a hundred shelves: each shelf counted its series by walking every card.
    void a_thousand_series_on_a_hundred_shelves_cost_a_few_looks_a_line()
    {
        aThousandSeriesCostAFewLooksALine(100, 10);
    }

    /// A line says whether its role can change exactly as its menu does: the ▾ of a chip
    /// whose menu offers nothing else is a door painted on a wall.
    void the_tree_says_where_a_role_can_change_as_its_menu_does()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        for (const QVariant &one : m_review->rail()) {
            const QVariantMap node = one.toMap();
            const QVariantList options = m_review->edits()->roleOptions(node.value(u"id"_s).toString());
            const bool other = std::ranges::any_of(options, [](const QVariant &option) {
                return option.toMap().value(u"possible"_s).toBool()
                       && !option.toMap().value(u"chosen"_s).toBool();
            });
            QCOMPARE(node.value(u"changeable"_s).toBool(), other);
        }
        QVERIFY(!railNamed(u"Vrac"_s).value(u"changeable"_s).toBool());
        QVERIFY(railNamed(u"Terres d'Arran"_s).value(u"changeable"_s).toBool());
    }

    /// A role no card can take yet — one of the cards it touches is still being read — is
    /// greyed with why, and its line shows no ▾: the menu offered it, the click did nothing,
    /// and nothing said that the folder was only not read yet.
    void a_role_no_card_can_take_yet_is_greyed_and_says_why()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const QString dragon = idNamed(u"Dragon Ball"_s);
        const auto option = [this](const QString &id, Shape::Role role) {
            for (const QVariant &one : m_review->edits()->roleOptions(id)) {
                if (one.toMap().value(u"role"_s).toInt() == int(role))
                    return one.toMap();
            }
            return QVariantMap();
        };
        QVERIFY(option(dragon, Shape::Role::Universe).value(u"possible"_s).toBool());
        sayWhatIsLacking(rowNamed(u"Dragon Ball"_s));

        // Corrected and confirmed, the card is announced again, and held there: its sheets
        // are still drawn, and no role can reach it until the answer comes.
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("POST /import ");
        };
        const int row = rowNamed(u"Dragon Ball"_s);
        QVERIFY(m_imports->edit(m_imports->cards().at(row).token, u"Perfect/Tome 1.cbz"_s,
                                u"number"_s, 7));
        m_queue->confirm();
        QTRY_COMPARE(stageOf(rowNamed(u"Dragon Ball"_s)), int(Imports::Stage::Asking));

        const QString again = idNamed(u"Dragon Ball"_s);
        const QVariantMap universe = option(again, Shape::Role::Universe);
        QVERIFY(!universe.value(u"possible"_s).toBool());
        QCOMPARE(universe.value(u"detail"_s).toString(), Words::roleWaitsForTheCards());
        QVERIFY(!railNamed(u"Dragon Ball"_s).value(u"changeable"_s).toBool());
        // And its chip says it too, to whoever does not see the menu's grey.
        const QString greyed = railNamed(u"Dragon Ball"_s).value(u"rolesGreyed"_s).toString();
        QVERIFY2(greyed.contains(Words::roleWaitsForTheCards()), qPrintable(greyed));
    }

    /// A role chip says to a screen reader which folder it ends, and what its greyed roles
    /// are: the menu's arrows pass over a greyed line, so its reason was read by nobody who
    /// does not see it.
    void a_role_chip_names_its_folder_and_its_greyed_roles()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);

        const QVariantMap vrac = railNamed(u"Vrac"_s);
        QCOMPARE(vrac.value(u"roleSpoken"_s).toString(),
                 Words::roleSpoken(u"Vrac"_s, Words::role(Shape::Role::Work)));
        QList<std::pair<QString, QString>> greyed;
        for (const QVariant &one : m_review->edits()->roleOptions(vrac.value(u"id"_s).toString())) {
            const QVariantMap option = one.toMap();
            if (!option.value(u"possible"_s).toBool())
                greyed.append({option.value(u"label"_s).toString(),
                               option.value(u"detail"_s).toString()});
        }
        QCOMPARE(greyed.size(), 4);
        QCOMPARE(vrac.value(u"rolesGreyed"_s).toString(), Words::rolesGreyed(greyed));
    }

    /// An archive no role reads is called what it is, an archive — « Tome » named it for what
    /// it would never be — and its line says in the alert's tokens that it will not be read;
    /// a folder read two ways says so under its title.
    void an_unread_archive_is_called_an_archive_and_says_it_will_not_be_read()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aUniverseWithArchivesAtItsRoot(true)});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Reviewing));
        const QString scan = idOf(0, u"scan_final.cbz"_s);
        QVERIFY(railNamed(u"scan_final.cbz"_s).value(u"unread"_s).toBool());
        QVERIFY(!railNamed(u"Le Combat"_s).value(u"unread"_s).toBool());

        m_review->view()->select(idOf(0, {}));
        for (const QVariantMap &line : lines())
            QCOMPARE(line.value(u"alarming"_s).toBool(), line.value(u"id"_s).toString() == scan);
        m_review->view()->select(scan);
        QCOMPARE(m_review->sheet().value(u"subtitle"_s).toString(), Words::anArchive());

        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 4);
        m_review->view()->select(idNamed(u"Dragon Ball"_s));
        QCOMPARE(m_review->sheet().value(u"subtitle"_s).toString(), Words::readingsPossible(2));
        m_review->view()->select(idNamed(u"Vrac"_s));
        // One reading only: « Vrac » is not offered as « lectures possibles ».
        QVERIFY(!m_review->sheet().value(u"subtitle"_s).toString().contains(u"lectures possibles"_s));
    }

    /// A reading chosen below is what the folder above now holds: the shelf over Dragon Ball
    /// went on counting it a series of two editions once it was a universe, and drew its one
    /// reading as not the one in force.
    void a_reading_chosen_below_is_counted_by_the_folder_above()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const QString shelf = u"drop/%1/"_s.arg(m_imports->cards().at(0).road.drop);

        QVERIFY(m_review->edits()->chooseReading(idNamed(u"Dragon Ball"_s), 1));
        m_review->view()->select(shelf);
        const QList<QVariantMap> readings = listOn(u"readings"_s);
        QCOMPARE(readings.size(), 1);
        QVERIFY(readings.at(0).value(u"chosen"_s).toBool());
        QCOMPARE(readings.at(0).value(u"summary"_s).toString(),
                 Words::readingSummary(2, 5, 0, 15));
    }

    /// A universe made a shelf is no universe any more, and the shelf above stops giving it
    /// as its reason: « Terres d'Arran est un univers » stood under the shelf after the click
    /// that made it one.
    void a_universe_made_a_shelf_is_no_reason_left_for_the_shelf_above()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const QString shelf = u"drop/%1/"_s.arg(m_imports->cards().at(0).road.drop);
        m_review->view()->select(shelf);
        const auto says = [this](const QString &text) {
            return std::ranges::any_of(listOn(u"clues"_s), [&text](const QVariantMap &one) {
                return one.value(u"text"_s).toString() == text;
            });
        };
        QVERIFY(says(Words::clueHoldsUniverse(u"Terres d'Arran"_s)));

        QVERIFY(m_review->edits()->setRole(idNamed(u"Terres d'Arran"_s), int(Shape::Role::Shelf)));
        m_review->view()->select(shelf);
        QVERIFY(!says(Words::clueHoldsUniverse(u"Terres d'Arran"_s)));
        QVERIFY(says(Words::clueHoldsAShelf(u"Terres d'Arran"_s)));
    }

    /// « Ce qui sera écrit » is what the announcement asks to write: every sidecar it sends
    /// that is not the disk's own bytes, by file name — and not a declaration the library
    /// already holds that nobody ticked, which the server leaves as it is. Counted from the
    /// composition alone, the sheet said `work.json × 1` over a series held whole whose
    /// `work.json` was never going to be written.
    void what_will_be_written_is_what_the_announcement_writes()
    {
        m_pretend->answerFor = sessions();
        m_offers->offer({aShelfOfUnsaidFolders()});
        QTRY_COMPARE(m_imports->reviewing(), 3);
        const int row = rowNamed(u"Dragon Ball"_s);
        const Card &card = m_imports->cards().at(row);
        QHash<QString, int> announced;
        for (const Manifest::Sidecar &one :
             Facts::compose(card.road.sheets, card.road.tree.sidecars).sidecars)
            ++announced[QFileInfo(one.path).fileName()];
        m_review->view()->select(idNamed(u"Dragon Ball"_s));
        QHash<QString, int> written;
        for (const QVariantMap &one : listOn(u"writes"_s))
            written.insert(one.value(u"name"_s).toString(), one.value(u"count"_s).toInt());
        written.remove(u"entry.json"_s);
        QCOMPARE(written, announced);

        cleanup();
        init();
        aHeldSeriesAnnounced(R"("creates":[],"declarations":[{"path":"work.json","present":{},)"
                             R"("presentName":"Blue Period","differs":["title"]}])");
        m_review->view()->select(idOf(0, {}));
        QVERIFY(std::ranges::none_of(listOn(u"writes"_s), [](const QVariantMap &one) {
            return one.value(u"name"_s).toString() == u"work.json"_s;
        }));
        m_imports->setDeclaring(0, u"work.json"_s, true);
        QVERIFY(std::ranges::any_of(listOn(u"writes"_s), [](const QVariantMap &one) {
            return one.value(u"name"_s).toString() == u"work.json"_s
                   && one.value(u"count"_s).toInt() == 1;
        }));
    }
    /// A universe lists its series once each, whatever tags they carry: « Terres d’Arran »
    /// was listed by tag, the same series repeated under every tag it held, and the line
    /// said « 76 tomes » where the question is how many editions there are.
    void a_universe_lists_each_series_once_whatever_its_tags()
    {
        const QString at = m_folder.filePath(u"Monde"_s);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        writeJson(at + u"/universe.json"_s, {{u"leaf"_s, 1}, {u"name"_s, u"Monde"_s}});
        aSaidSeries(at + u"/Alpha"_s, u"Alpha"_s, 1,
                    {{u"tags"_s, QJsonArray{u"Cycle"_s, u"Prequel"_s}}});
        aSaidSeries(at + u"/Beta"_s, u"Beta"_s, 1, {{u"tags"_s, QJsonArray{u"Cycle"_s}}});
        aSaidSeries(at + u"/Gamma"_s, u"Gamma"_s, 1);
        offeredAndRead({at});
        m_review->view()->select(idOf(0, {}));

        QStringList names;
        for (const QVariantMap &one : lines())
            names << one.value(u"name"_s).toString();
        QCOMPARE(names, (QStringList{u"Alpha"_s, u"Beta"_s, u"Gamma"_s}));
    }

    /// Tags are given as words and leave as a list: typed with commas, a tag holding one was
    /// two tags, and a string written where the server reads an array is no tag at all.
    void editing_tags_as_words_writes_a_list()
    {
        offeredAndRead({aUniverse()});
        const QString arran = u"Terres d’Arran"_s;
        const QString elfes = idOf(0, u"Elfes"_s);

        QVERIFY(!m_review->edits()->edit(elfes, u"tags"_s, arran));
        QVERIFY(m_review->edits()->editWords(elfes, u"tags"_s, {arran}));
        QCOMPARE(announced(0, u"Elfes/work.json"_s).value(u"tags"_s).toArray(),
                 QJsonArray{arran});
        m_review->view()->select(elfes);
        QCOMPARE(fieldOnSheet(u"tags"_s).value(u"words"_s).toStringList(), QStringList{arran});
        QCOMPARE(fieldOnSheet(u"tags"_s).value(u"vocabulary"_s).toStringList(),
                 QStringList{arran});

        // No word is no tag: the key leaves the file rather than holding an empty list.
        QVERIFY(m_review->edits()->editWords(elfes, u"tags"_s, {}));
        QVERIFY(!announced(0, u"Elfes/work.json"_s).contains(u"tags"_s));
        m_review->view()->select(idOf(0, {}));
        QCOMPARE(headers(), QStringList{Words::reviewGroup(Words::Reviewed::Ready, 2)});
    }

    /// A word a field holds elsewhere in the drop is offered first by how often it is met:
    /// the drawer of eight volumes is not typed a ninth time, differently.
    void a_list_field_offers_the_words_of_the_drop_most_frequent_first()
    {
        offeredAndRead({aCreditedSeries()});
        m_review->view()->select(idOf(0, u"Tome 3.cbz"_s));
        QCOMPARE(fieldOnSheet(u"artists"_s).value(u"words"_s).toStringList(), QStringList());
        const QStringList offered =
            fieldOnSheet(u"artists"_s).value(u"vocabulary"_s).toStringList();
        // Writer and drawer are one pool of people: « Toriyama » typed for the writing was
        // offered there and not for the drawing, though he does both. Met three times each,
        // « Auteur A » (writing) and « Auteur B » (drawing) head the same list in both fields.
        QCOMPARE(offered.first(2), (QStringList{u"Auteur A"_s, u"Auteur B"_s}));
        QCOMPARE(fieldOnSheet(u"authors"_s).value(u"vocabulary"_s).toStringList().first(2),
                 offered.first(2));
        QCOMPARE(fieldOnSheet(u"authors"_s).value(u"words"_s).toStringList(),
                 QStringList{u"Auteur E"_s});
    }

    /// A universe's sheet lists its reading orders, each with what it reads, and says which
    /// is the default and which another one holds — « Terres d’Arran », written again inside
    /// « Tout Aquilon ». The orders are no box among the fields: typed there, they were JSON.
    /// Tags put on series in a universe leave the box under the tree alone: it said
    /// « « Terres d’Arran » est une étiquette… », which only repeated a `work.json`, the
    /// same words whichever line was chosen, about something the import never does.
    void a_universe_whose_series_carry_a_tag_says_nothing_of_it_under_the_tree()
    {
        offeredAndRead({anAquilon()});
        QCOMPARE(m_review->railNote(), Words::everythingSaid());
    }

    void the_universe_sheet_lists_its_orders_and_what_one_copies()
    {
        offeredAndRead({anAquilon()});
        const QString arran = u"Terres d’Arran"_s;
        m_review->view()->select(idOf(0, {}));

        const QList<QVariantMap> orders = listOn(u"orders"_s);
        QCOMPARE(orders.size(), 2);
        QCOMPARE(orders.at(0).value(u"id"_s).toString(), u"tout-aquilon"_s);
        QCOMPARE(orders.at(0).value(u"name"_s).toString(), u"Tout Aquilon"_s);
        QCOMPARE(orders.at(0).value(u"line"_s).toString(),
                 u"5 séries · 7 étapes, dont 3 recopiées"_s);
        QVERIFY(orders.at(0).value(u"isDefault"_s).toBool());
        QVERIFY(!orders.at(0).value(u"subset"_s).toBool());
        QCOMPARE(orders.at(1).value(u"name"_s).toString(), arran);
        QCOMPARE(orders.at(1).value(u"line"_s).toString(), u"3 séries · 3 étapes"_s);
        QVERIFY(!orders.at(1).value(u"isDefault"_s).toBool());
        QVERIFY(orders.at(1).value(u"subset"_s).toBool());
        QCOMPARE(m_review->sheet().value(u"ordersNote"_s).toString(),
                 Words::ordersCopied(arran, u"Tout Aquilon"_s));
        QVERIFY(m_review->sheet().value(u"ordersEditable"_s).toBool());
        QVERIFY(fieldOnSheet(u"orders"_s).isEmpty());
        QVERIFY(fieldOnSheet(u"defaultOrder"_s).isEmpty());
        QCOMPARE(m_review->subtitle(),
                 u"1 univers · 5 séries · universe.json et 2 ordres de lecture"_s);
    }

    /// Required says so filled as well as empty: the title of a complete series is required
    /// and lacks nothing, its genres are neither.
    void a_required_field_says_so_even_filled()
    {
        offeredAndRead({aCompleteSeries()});
        m_review->view()->select(idOf(0, {}));
        QVERIFY(fieldOnSheet(u"title"_s).value(u"required"_s).toBool());
        QVERIFY(!fieldOnSheet(u"title"_s).value(u"lacking"_s).toBool());
        QVERIFY(fieldOnSheet(u"readingDirection"_s).value(u"required"_s).toBool());
        QVERIFY(fieldOnSheet(u"genres"_s).contains(u"required"_s));
        QVERIFY(!fieldOnSheet(u"genres"_s).value(u"required"_s).toBool());
    }

    /// A volume that names no drawer under a series that names Morris is drawn by Morris: its
    /// field shows him, greyed, and is not « Manquant » — it was a red empty box beside a
    /// footer saying nothing was missing.
    void a_volume_credit_from_its_series_is_inherited_not_missing()
    {
        offeredAndRead({aCreditedSeries({{u"artists"_s, QJsonArray{u"Morris"_s}}})});
        m_review->view()->select(idOf(0, u"Tome 3.cbz"_s));
        const QVariantMap drawing = fieldOnSheet(u"artists"_s);
        QVERIFY(drawing.value(u"required"_s).toBool());
        QVERIFY(drawing.value(u"inherited"_s).toBool());
        QVERIFY(!drawing.value(u"lacking"_s).toBool());
        QCOMPARE(drawing.value(u"inheritedWords"_s).toStringList(), QStringList{u"Morris"_s});
        QCOMPARE(drawing.value(u"inheritedText"_s).toString(), u"Morris"_s);
        QCOMPARE(drawing.value(u"words"_s).toStringList(), QStringList());
        const QVariantMap writing = fieldOnSheet(u"authors"_s);
        QVERIFY(!writing.value(u"inherited"_s).toBool());
        QCOMPARE(writing.value(u"words"_s).toStringList(), QStringList{u"Auteur E"_s});
    }

    /// An edition whose `edition.json` says nothing of its publisher, language, direction or
    /// status takes the work's, and its sheet says it takes them: they read as its own, and
    /// nothing told that changing the series would change them too.
    void an_edition_shows_what_it_takes_from_the_work_as_inherited()
    {
        const QString series = m_folder.filePath(u"Serie"_s);
        const QString edition = series + u"/Ed"_s;
        QVERIFY(QDir().mkpath(edition));
        writeJson(series + u"/work.json"_s,
                  QJsonObject{{u"leaf"_s, 1}, {u"title"_s, u"Serie"_s},
                              {u"readingDirection"_s, u"RIGHT_TO_LEFT"_s},
                              {u"publisher"_s, u"Dupuis"_s}, {u"language"_s, u"fr"_s},
                              {u"status"_s, u"completed"_s},
                              {u"authors"_s, QJsonArray{u"Morris"_s}},
                              {u"artists"_s, QJsonArray{u"Morris"_s}}});
        writeJson(edition + u"/edition.json"_s,
                  QJsonObject{{u"leaf"_s, 1}, {u"name"_s, u"Ed"_s}, {u"collection"_s, u"Rouge"_s}});
        aVolume(edition + u"/Tome 1.cbz"_s, 1);
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            return aReply(200, R"({"id":"imp_e","root":"Serie","toSend":["Ed/Tome 1.cbz"],)"
                               R"("alreadyThere":[],"bytesToSend":10,"creates":[]})");
        };
        m_offers->offer({series});
        QTRY_VERIFY(stageOf(0) == int(Imports::Stage::Ready)
                    || stageOf(0) == int(Imports::Stage::Reviewing));
        m_review->view()->open();
        m_review->view()->select(idOf(0, u"Ed"_s));
        for (const QString &key : {u"publisher"_s, u"language"_s, u"readingDirection"_s,
                                   u"status"_s}) {
            const QVariantMap one = fieldOnSheet(key);
            QVERIFY2(one.value(u"inherited"_s).toBool(), qPrintable(key));
            QCOMPARE(one.value(u"inheritedText"_s).toString(), one.value(u"text"_s).toString());
            QVERIFY2(!one.value(u"lacking"_s).toBool(), qPrintable(key));
        }
        QCOMPARE(fieldOnSheet(u"publisher"_s).value(u"inheritedText"_s).toString(), u"Dupuis"_s);
        QVERIFY(!fieldOnSheet(u"collection"_s).value(u"inherited"_s).toBool());
        QVERIFY(!fieldOnSheet(u"name"_s).value(u"inherited"_s).toBool());

        // Typed over, then emptied: the edition writes nothing, the server reads the series'
        // value again, and the sheet said « Manquant » in red and held the import back on a
        // reading direction `work.json` gives.
        QVERIFY(m_review->edits()->edit(idOf(0, u"Ed"_s), u"readingDirection"_s, u"LEFT_TO_RIGHT"_s));
        QVERIFY(!fieldOnSheet(u"readingDirection"_s).value(u"inherited"_s).toBool());
        QVERIFY(m_review->edits()->edit(idOf(0, u"Ed"_s), u"readingDirection"_s, QString()));
        const QVariantMap emptied = fieldOnSheet(u"readingDirection"_s);
        QVERIFY(emptied.value(u"inherited"_s).toBool());
        QVERIFY(!emptied.value(u"inheritedText"_s).toString().isEmpty());
        QVERIFY(!emptied.value(u"lacking"_s).toBool());
    }

    /// The series' Scénario and Dessin say what they are a default for, in the word the series
    /// counts its volumes in; no other field of any sheet carries a hint.
    void the_series_credits_carry_their_hint()
    {
        offeredAndRead({aCreditedSeries({{u"medium"_s, u"bd"_s}})});
        m_review->view()->select(idOf(0, {}));
        QCOMPARE(fieldOnSheet(u"authors"_s).value(u"hint"_s).toString(),
                 Words::creditDefault(false, Api::Medium::Bd));
        QCOMPARE(fieldOnSheet(u"artists"_s).value(u"hint"_s).toString(),
                 Words::creditDefault(true, Api::Medium::Bd));
        QVERIFY(fieldOnSheet(u"genres"_s).value(u"hint"_s).toString().isEmpty());
        m_review->view()->select(idOf(0, u"Tome 1.cbz"_s));
        QVERIFY(fieldOnSheet(u"authors"_s).value(u"hint"_s).toString().isEmpty());
    }

    /// A series whose volumes are credited shows what it computes from them, read-only: the
    /// people of the nine volumes once each, twelve, and the series writing none of them.
    void a_series_sheet_shows_the_credits_it_computes_read_only()
    {
        offeredAndRead({aCreditedSeries()});
        m_review->view()->select(idOf(0, {}));

        const QVariantMap credits = m_review->sheet().value(u"credits"_s).toMap();
        QCOMPARE(credits.value(u"count"_s).toInt(), 12);
        QVERIFY(!credits.value(u"written"_s).toBool());
        QCOMPARE(credits.value(u"computed"_s).toString(),
                 u"Auteur A, Auteur B, Auteur C et 9 autres"_s);
        QCOMPARE(credits.value(u"all"_s).toStringList().size(), 12);
        QVERIFY(!credits.contains(u"note"_s));
        QCOMPARE(fieldOnSheet(u"authors"_s).value(u"words"_s).toStringList(), QStringList());
    }

    /// A volume that names nobody is credited with the series' default, and the union is
    /// made of what each volume is credited with, as the server's: Lucky Luke with Morris in
    /// `work.json` and one volume « Jul » listed Jul alone and said « 1 auteur au total ».
    void the_series_union_counts_the_default_a_volume_inherits()
    {
        offeredAndRead({aCreditedSeries({{u"artists"_s, QJsonArray{u"Morris"_s}}})});
        m_review->view()->select(idOf(0, {}));
        const QVariantMap credits = m_review->sheet().value(u"credits"_s).toMap();
        QCOMPARE(credits.value(u"count"_s).toInt(), 13);
        QVERIFY(credits.value(u"all"_s).toStringList().contains(u"Morris"_s));
    }

    /// Authors written by hand win for the series, and going back to the computed ones
    /// removes them — the singular `author` an older `work.json` wrote included: left there,
    /// it was read as the series' authors again, and the union never came back.
    void writing_series_authors_by_hand_wins_and_going_back_removes_them()
    {
        offeredAndRead({aCreditedSeries({{u"author"_s, u"Ancien"_s}})});
        const QString series = idOf(0, {});
        m_review->view()->select(series);
        QVERIFY(m_review->sheet().value(u"credits"_s).toMap().value(u"written"_s).toBool());

        QVERIFY(m_review->edits()->editWords(series, u"authors"_s, {u"Collectif"_s}));
        QVariantMap credits = m_review->sheet().value(u"credits"_s).toMap();
        QVERIFY(credits.value(u"written"_s).toBool());
        QCOMPARE(credits.value(u"writtenText"_s).toString(), u"Collectif"_s);
        QCOMPARE(credits.value(u"count"_s).toInt(), 12);
        QJsonObject work = announced(0, u"work.json"_s);
        QCOMPARE(work.value(u"authors"_s).toArray(), QJsonArray{u"Collectif"_s});
        QVERIFY(!work.contains(u"author"_s));

        QVERIFY(m_review->edits()->editWords(series, u"authors"_s, {}));
        credits = m_review->sheet().value(u"credits"_s).toMap();
        QVERIFY(!credits.value(u"written"_s).toBool());
        work = announced(0, u"work.json"_s);
        QVERIFY2(!work.contains(u"authors"_s) && !work.contains(u"author"_s),
                 QJsonDocument(work).toJson().constData());
        QCOMPARE(work.value(u"title"_s).toString(), u"Elfes"_s);
    }

    /// A volume credited with nobody for the drawing, among eight that are, is to review and
    /// its chip names the field — read « Dit » like the others, it was the one line not to
    /// skip and looked like the eight — and it blocks nothing.
    /// A volume drawn by nobody, under a series that names no drawer, holds its series back:
    /// it is to review, its chip names what it lacks, and the footer counts it once — on the
    /// series, where a drawer typed releases it. It was a doubt that held nothing back.
    void a_volume_missing_a_credit_is_to_review_and_holds_its_series_back()
    {
        offeredAndRead({aCreditedSeries()});
        m_review->view()->select(idOf(0, {}));

        QCOMPARE(countOf(Review::Filter::ToReview).value(u"count"_s).toInt(), 1);
        QCOMPARE(countOf(Review::Filter::Ready).value(u"count"_s).toInt(), 8);
        QCOMPARE(m_review->importLabel(), u"Importer"_s);
        const QVariantMap third = lineNamed(u"Tome 3"_s);
        QCOMPARE(third.value(u"chip"_s).toString(), u"Dessin"_s);
        QCOMPARE(third.value(u"origin"_s).toInt(), int(Review::Origin::Missing));
        // A volume that lacks nothing is worth no word: it wore « Écrit » like every other.
        QCOMPARE(lineNamed(u"Tome 1"_s).value(u"chip"_s).toString(), QString());
        QVERIFY2(m_review->footer().endsWith(Words::creditsFooter(12, 1)),
                 qPrintable(m_review->footer()));
    }

    /// The list of a series whose volumes are credited apart says each volume's credits on
    /// its line: « Auteur F » drew volume 4 alone, and nothing but nine sheets said it.
    void the_list_of_a_credited_series_shows_each_volume_s_credits()
    {
        offeredAndRead({aCreditedSeries()});
        m_review->view()->select(idOf(0, {}));

        QCOMPARE(m_review->listTitle(), u"Tomes d’Elfes"_s + Words::Nbsp + u"· crédits"_s);
        QCOMPARE(lineNamed(u"Tome 1"_s).value(u"detail"_s).toString(),
                 u"Scénario"_s + Words::Nbsp + u": Auteur A"_s + Words::Nbsp + u"· Dessin"_s
                     + Words::Nbsp + u": Auteur B"_s);
        QCOMPARE(lineNamed(u"Tome 4"_s).value(u"detail"_s).toString(),
                 Words::creditsLine({u"Auteur A"_s}, {u"Auteur F"_s}));
        QCOMPARE(lineNamed(u"Tome 3"_s).value(u"detail"_s).toString(),
                 Words::creditsLine({u"Auteur E"_s}, {}));
    }

    /// A drop that says no tag, no order and no credit apart keeps the frame it had: its
    /// groups, its line under the title, its footer and its note. A series every volume of
    /// which credits the same person is one of them — its list says nothing a series' own
    /// line does not — and its sheet still shows whom it is credited to.
    void a_drop_without_tags_credits_or_orders_keeps_the_frame_it_had()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (!request.startsWith("POST /import "))
                return QByteArray();
            if (request.contains("\"root\":\"Terres\""))
                return anOpening("Terres", {u"Elfes/Tome 1.cbz"_s, u"Mages/Tome 1.cbz"_s}, {});
            return anOpening("Koro", {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s}, {});
        };
        m_offers->offer({aUniverse(), aCompleteSeries()});
        QTRY_COMPARE(stageOf(0), int(Imports::Stage::Ready));
        QTRY_COMPARE(stageOf(1), int(Imports::Stage::Ready));
        m_review->view()->open();
        m_review->view()->select(idOf(0, {}));

        QCOMPARE(headers(), QStringList{Words::reviewGroup(Words::Reviewed::Ready, 2)});
        QCOMPARE(m_review->subtitle(), Words::apportSummary(1, 3, 5, Words::Source::Leaf));
        QCOMPARE(m_review->footer(), Words::reviewFooter(0, 0));
        QCOMPARE(m_review->railNote(), Words::everythingSaid());
        QCOMPARE(lineNamed(u"Elfes"_s).value(u"detail"_s).toString(),
                 Words::nodeHolds(1, QFileInfo(m_folder.filePath(u"Terres/Elfes/Tome 1.cbz"_s))
                                         .size()));
        // Every sheet carries the keys, empty where it says nothing: one shape to bind to.
        QVERIFY(m_review->sheet().contains(u"orders"_s));
        QVERIFY(listOn(u"orders"_s).isEmpty());
        QVERIFY(m_review->sheet().value(u"credits"_s).toMap().isEmpty());
        QCOMPARE(m_review->sheet().value(u"ordersNote"_s).toString(), QString());

        cleanup();
        init();
        aKomgaSeriesAnnounced();
        m_review->view()->open();
        QCOMPARE(m_review->listTitle(), Words::volumesOf(u"Blue Period"_s));
        QCOMPARE(m_review->footer(),
                 Words::waitingSeries(1, 0, 0) + u' '
                     + u"1 valeur manque encore · 2 archives seront réécrites"_s);
        QCOMPARE(lineNamed(u"Tome 13"_s).value(u"detail"_s).toString(),
                 Words::fileDetail(komgaName(13),
                                   QFileInfo(m_folder.filePath(u"Blue Period/"_s + komgaName(13)))
                                       .size()));
        const QVariantMap credits = m_review->sheet().value(u"credits"_s).toMap();
        QCOMPARE(credits.value(u"all"_s).toStringList(), QStringList{u"Tsubasa Yamaguchi"_s});
    }
    /// A long series whose volumes are credited apart is walked for its credits once per
    /// change, whatever reads them — its sheet, its list, the title over it and the footer:
    /// each walking it again, a series of three hundred volumes was read four times on every
    /// change of the queue.
    void the_credits_of_a_long_series_are_walked_once_whatever_reads_them()
    {
        constexpr int Volumes = 300;
        const QString at = m_folder.filePath(u"Long"_s);
        aSaidSeries(at, u"Long"_s, 0, {}, false);
        for (int number = 1; number <= Volumes; ++number) {
            const QJsonObject entry{{u"leaf"_s, 1},
                                    {u"number"_s, number},
                                    {u"authors"_s, QJsonArray{u"Auteur %1"_s.arg(number % 7)}},
                                    {u"artists"_s, QJsonArray{u"Auteur %1"_s.arg(number % 7)}}};
            QFile file(at + u"/Tome %1.cbz"_s.arg(number));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(Zip::volume(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
        }
        offeredAndRead({at});
        m_review->view()->select(idOf(0, {}));
        emit m_imports->changed();

        const qint64 before = Review::cost().credits;
        const QVariantMap credits = m_review->sheet().value(u"credits"_s).toMap();
        const QList<QVariantMap> listed = lines();
        const QString title = m_review->listTitle();
        const QString footer = m_review->footer();
        const qint64 walked = Review::cost().credits - before;

        QCOMPARE(credits.value(u"count"_s).toInt(), 7);
        QCOMPARE(listed.size(), Volumes);
        QCOMPARE(title, Words::volumesCredited(u"Long"_s));
        QCOMPARE(footer, Words::creditsFooter(7));
        const qint64 sheets = m_imports->cards().at(0).road.sheets.size();
        QVERIFY2(walked <= sheets, qPrintable(u"%1 looks for %2 sheets"_s.arg(walked).arg(sheets)));
    }
    /// A universe of two thousand orders, each including the next, is drawn at a cost bounded
    /// by what the server lets a universe look at, not by the square of its orders: a plan
    /// built again for each inclusion, and each order walking its own way down, was a hundred
    /// million steps on every change of the queue.
    void a_universe_of_chained_orders_is_drawn_at_a_bounded_cost()
    {
        constexpr int Chained = 2000;
        const QString at = m_folder.filePath(u"Chaine"_s);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        QJsonArray orders;
        for (int one = 0; one < Chained; ++one) {
            QJsonArray steps{QJsonObject{{u"work"_s, u"Alpha"_s}, {u"from"_s, one + 1},
                                         {u"to"_s, one + 1}}};
            if (one + 1 < Chained)
                steps << QJsonObject{{u"include"_s, u"o%1"_s.arg(one + 1)}};
            orders << QJsonObject{{u"id"_s, u"o%1"_s.arg(one)}, {u"steps"_s, steps}};
        }
        writeJson(at + u"/universe.json"_s,
                  {{u"leaf"_s, 1}, {u"name"_s, u"Chaine"_s}, {u"orders"_s, orders}});
        aSaidSeries(at + u"/Alpha"_s, u"Alpha"_s, 1);
        offeredAndRead({at});
        m_review->view()->select(idOf(0, {}));

        const qint64 before = Review::cost().orders;
        const QVariantList drawn = m_review->sheet().value(u"orders"_s).toList();
        const qint64 looked = Review::cost().orders - before;

        QCOMPARE(drawn.size(), Chained);
        // The last order reads its one step, as the server records it.
        QCOMPARE(drawn.constLast().toMap().value(u"line"_s).toString(),
                 Words::orderLine(1, 1, 0, false));
        const qint64 written = 2 * Chained;
        QVERIFY2(looked <= Orders::MostUniverseLooks + 4 * written,
                 qPrintable(u"%1 steps looked at for %2 written"_s.arg(looked).arg(written)));
    }

    /// An inclusion the announcement would refuse is said over the universe's sheet, loudly:
    /// the orders are no field of it any more, and the doubt they carried was shown nowhere
    /// while it held the whole card back.
    void a_refused_inclusion_is_said_over_the_universe_sheet()
    {
        const QString at = m_folder.filePath(u"Monde"_s);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        const QJsonArray steps{QJsonObject{{u"include"_s, u"nowhere"_s}}};
        writeJson(at + u"/universe.json"_s,
                  {{u"leaf"_s, 1},
                   {u"name"_s, u"Monde"_s},
                   {u"orders"_s, QJsonArray{QJsonObject{{u"id"_s, u"tout"_s},
                                                        {u"name"_s, u"Tout"_s},
                                                        {u"steps"_s, steps}}}}});
        aSaidSeries(at + u"/Alpha"_s, u"Alpha"_s, 1);
        offeredAndRead({at});
        m_review->view()->select(idOf(0, {}));

        QVERIFY(sheetSays(Words::orderRefused(u"tout"_s, 1, Orders::Refusal::NamesNothing,
                                              u"nowhere"_s),
                          true));
        QCOMPARE(listOn(u"orders"_s).size(), 1);
    }
};

QTEST_MAIN(HoldsTheReview)
#include "holds_the_review.moc"
