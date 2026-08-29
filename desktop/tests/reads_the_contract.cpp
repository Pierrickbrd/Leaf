// What the parser does with what the server actually sends.
//
// The interesting cases are not the happy one. They are: a required field the server forgot,
// a `null` that must not become an empty string, and a word this client has never heard of.
// The first two must fail loudly, the third must not fail at all — and the difference between
// those two reactions is the whole design.
//
// Fixtures are built as QJsonObject rather than parsed from text, because moc cannot read a
// raw string literal and escaped JSON is unreadable. It also means a typo in a field name is
// a typo in one place.

#include "Api.h"

#include "Ascii.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

QJsonObject aSeries()
{
    return QJsonObject{
        {"id"_ascii, "ed-1"_ascii},
        {"workId"_ascii, "wk-1"_ascii},
        {"name"_ascii, "Assassination Classroom"_ascii},
        {"work"_ascii, "Assassination Classroom"_ascii},
        {"entryCount"_ascii, 21},
        {"chapterCount"_ascii, 180},
        {"arcCount"_ascii, 0},
        {"universe"_ascii, QJsonValue::Null},
        {"edition"_ascii, QJsonValue::Null},
        {"author"_ascii, u"Yūsei Matsui"_s},
        {"medium"_ascii, "manga"_ascii},
        {"readingDirection"_ascii, "RIGHT_TO_LEFT"_ascii},
        {"status"_ascii, "completed"_ascii},
        {"readStatus"_ascii, "IN_PROGRESS"_ascii},
        {"declaredVolumes"_ascii, 21},
        {"ownedVolumes"_ascii, 21},
        {"missingVolumes"_ascii, QJsonArray{}},
        {"missingChapters"_ascii, QJsonArray{3.5}},
        {"genres"_ascii, QJsonArray{"Action"_ascii, u"Comédie"_s}},
        {"publisher"_ascii, "Kana"_ascii},
        {"language"_ascii, "fr"_ascii},
        {"addedAt"_ascii, 1750000000},
        {"lastAddedAt"_ascii, 1755000000},
    };
}

QJsonObject anEntry()
{
    return QJsonObject{
        {"id"_ascii, "en-12"_ascii},
        {"type"_ascii, "VOLUME"_ascii},
        {"number"_ascii, 12},
        {"title"_ascii, QJsonValue::Null},
        {"pageCount"_ascii, 190},
        {"chapterCount"_ascii, 4},
        {"file"_ascii, "Tome 12.cbz"_ascii},
        {"size"_ascii, 61234567},
    };
}

} // namespace

class ReadsTheContract : public QObject
{
    Q_OBJECT

private slots:
    void a_whole_series_arrives_intact()
    {
        const Api::Read<Api::Series> got = Api::series(aSeries());
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        const Api::Series &one = *got.value;

        QCOMPARE(one.id, u"ed-1"_s);
        QCOMPARE(one.name, u"Assassination Classroom"_s);
        QCOMPARE(one.entryCount, 21);
        QCOMPARE(one.chapterCount, 180);
        QCOMPARE(one.ownedVolumes, 21);
        QCOMPARE(one.declaredVolumes, std::optional<int>(21));
        QCOMPARE(one.author, std::optional<QString>(u"Yūsei Matsui"_s));
        QCOMPARE(one.medium, std::optional<Api::Medium>(Api::Medium::Manga));
        QCOMPARE(one.readingDirection,
                 std::optional<Api::ReadingDirection>(Api::ReadingDirection::RightToLeft));
        QCOMPARE(one.run, std::optional<Api::Run>(Api::Run::Completed));
        QCOMPARE(one.readStatus, Api::ReadStatus::InProgress);
        QCOMPARE(one.genres, QList<QString>({u"Action"_s, u"Comédie"_s}));
        QCOMPARE(one.missingChapters, QList<double>({3.5}));
        QVERIFY(one.missingVolumes.isEmpty());
        QCOMPARE(one.lastAddedAt, std::optional<qint64>(1755000000));
    }

    /// Null is how the contract says "not recorded". An empty string would say "recorded as
    /// nothing", and the shelf would print an empty line where it should print nothing.
    void null_is_absent_and_not_an_empty_string()
    {
        const Api::Read<Api::Series> got = Api::series(aSeries());
        QVERIFY(got.ok());
        QVERIFY(!got.value->universe.has_value());
        QVERIFY(!got.value->edition.has_value());
    }

    /// A field the contract marks required is the server's promise. Breaking it is worth a
    /// sentence naming the field, not a series with a blank name.
    void a_missing_required_field_refuses_and_says_which()
    {
        QJsonObject without = aSeries();
        without.remove("name"_ascii);

        const Api::Read<Api::Series> got = Api::series(without);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"name"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"missing"_s), qPrintable(got.trouble));
    }

    void a_required_field_set_to_null_refuses_too()
    {
        QJsonObject nulled = aSeries();
        nulled["work"_ascii] = QJsonValue::Null;

        const Api::Read<Api::Series> got = Api::series(nulled);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"work"_s), qPrintable(got.trouble));
    }

    /// The other half of the rule: the server may learn a word before this client does, and a
    /// shelf that emptied itself over one would be worse than a shelf with one odd tile.
    void a_word_the_client_never_heard_keeps_the_row()
    {
        QJsonObject odd = aSeries();
        odd["medium"_ascii] = "lianhuanhua"_ascii;

        const Api::Read<Api::Series> got = Api::series(odd);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->medium, std::optional<Api::Medium>(Api::Medium::Other));
        QCOMPARE(got.value->name, u"Assassination Classroom"_s);
    }

    void a_page_says_where_the_broken_item_is()
    {
        QJsonObject broken = aSeries();
        broken.remove("arcCount"_ascii);

        const QJsonObject body{
            {"total"_ascii, 3},
            {"page"_ascii, 0},
            {"size"_ascii, 100},
            {"items"_ascii, QJsonArray{aSeries(), broken, aSeries()}},
        };

        const Api::Read<Api::Page> got = Api::page(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"items[1]"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"arcCount"_s), qPrintable(got.trouble));
    }

    void a_good_page_keeps_its_counts()
    {
        const QJsonObject body{
            {"total"_ascii, 337},
            {"page"_ascii, 2},
            {"size"_ascii, 100},
            {"items"_ascii, QJsonArray{aSeries(), aSeries()}},
        };

        const Api::Read<Api::Page> got = Api::page(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->total, 337);
        QCOMPARE(got.value->page, 2);
        QCOMPARE(got.value->items.size(), 2);
    }

    /// "Tome 12 · Page 47/190 · Chapitre 98" — three facts from three places. The chapter's
    /// wording is the server's, so the band and the reader cannot disagree about it.
    void the_band_reads_where_you_stopped()
    {
        const QJsonObject body{
            {"seriesId"_ascii, "ed-1"_ascii},
            {"seriesName"_ascii, "Assassination Classroom"_ascii},
            {"reason"_ascii, "IN_PROGRESS"_ascii},
            {"entry"_ascii, anEntry()},
            {"progress"_ascii,
             QJsonObject{
                 {"entryId"_ascii, "en-12"_ascii},
                 {"page"_ascii, 47},
                 {"pageCount"_ascii, 190},
                 {"finished"_ascii, false},
                 {"updatedAt"_ascii, 1755000000},
                 {"chapter"_ascii, QJsonObject{{"id"_ascii, "ch-98"_ascii},
                                            {"raw"_ascii, "098"_ascii},
                                            {"label"_ascii, "Chapitre 98"_ascii},
                                            {"kind"_ascii, "CHAPTER"_ascii},
                                            {"position"_ascii, 2},
                                            {"entryId"_ascii, "en-12"_ascii}}},
             }},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->reason, Api::UpNext::Reason::InProgress);
        QCOMPARE(got.value->entryKind, Api::UpNext::Kind::Volume);
        QCOMPARE(got.value->entryNumber, std::optional<double>(12));
        QCOMPARE(got.value->page, std::optional<int>(47));
        QCOMPARE(got.value->pageCount, 190);
        QCOMPARE(got.value->chapterLabel, std::optional<QString>(u"Chapitre 98"_s));
    }

    /// Nothing started is not a fault, and the band words it differently. Absent progress must
    /// therefore parse, and must leave `page` empty rather than sitting it at zero.
    void nothing_started_parses_with_no_page()
    {
        const QJsonObject body{
            {"seriesId"_ascii, "ed-1"_ascii},
            {"seriesName"_ascii, "Berserk"_ascii},
            {"reason"_ascii, "NEXT_UP"_ascii},
            {"entry"_ascii, anEntry()},
            {"progress"_ascii, QJsonValue::Null},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->reason, Api::UpNext::Reason::NextUp);
        QVERIFY(!got.value->page.has_value());
        QVERIFY(!got.value->chapterLabel.has_value());
        QCOMPARE(got.value->pageCount, 190);
    }

    void a_band_card_with_no_entry_refuses()
    {
        const QJsonObject body{
            {"seriesId"_ascii, "ed-1"_ascii},
            {"seriesName"_ascii, "Berserk"_ascii},
            {"reason"_ascii, "NEXT_UP"_ascii},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"entry"_s), qPrintable(got.trouble));
    }

    /// The panel exists to show what you own, so a facet with no count is not a facet.
    void facets_keep_their_counts_and_drop_what_has_none()
    {
        const QJsonObject body{
            {"media"_ascii, QJsonArray{QJsonObject{{"value"_ascii, "manga"_ascii}, {"count"_ascii, 41}},
                                    QJsonObject{{"value"_ascii, "bd"_ascii}, {"count"_ascii, 12}},
                                    QJsonObject{{"value"_ascii, "comics"_ascii}}}},
            {"readStatuses"_ascii,
             QJsonArray{QJsonObject{{"value"_ascii, "UNREAD"_ascii}, {"count"_ascii, 12}}}},
        };

        const Api::Read<Api::Facets> got = Api::facets(body);
        QVERIFY(got.ok());
        QCOMPARE(got.value->media.size(), 2);
        QCOMPARE(got.value->media.at(0).value, u"manga"_s);
        QCOMPARE(got.value->media.at(0).count, 41);
        QCOMPARE(got.value->readStatuses.size(), 1);
        QVERIFY(got.value->genres.isEmpty());
    }

    /// Walked rather than trusted: every spelling the contract lists must survive going in and
    /// coming back out. A switch that forgot a case would show up here and nowhere else.
    void the_contract_spellings_survive_the_round_trip()
    {
        for (const QString &word : {u"manga"_s, u"bd"_s, u"comics"_s, u"manhwa"_s, u"manhua"_s,
                                    u"webtoon"_s, u"artbook"_s, u"other"_s})
            QCOMPARE(Api::spell(Api::medium(word)), word);

        for (const QString &word : {u"UNREAD"_s, u"IN_PROGRESS"_s, u"READ"_s})
            QCOMPARE(Api::spell(Api::readStatus(word)), word);
    }
};

QTEST_APPLESS_MAIN(ReadsTheContract)
#include "reads_the_contract.moc"
