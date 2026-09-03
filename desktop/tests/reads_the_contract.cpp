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

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

QJsonObject aSeries()
{
    return QJsonObject{
        {u"id"_s, u"ed-1"_s},
        {u"workId"_s, u"wk-1"_s},
        {u"name"_s, u"Assassination Classroom"_s},
        {u"work"_s, u"Assassination Classroom"_s},
        {u"entryCount"_s, 21},
        {u"chapterCount"_s, 180},
        {u"arcCount"_s, 0},
        {u"universe"_s, QJsonValue::Null},
        {u"edition"_s, QJsonValue::Null},
        {u"author"_s, u"Yūsei Matsui"_s},
        {u"authors"_s, QJsonArray{u"Yūsei Matsui"_s}},
        {u"artists"_s, QJsonArray{u"Yūsei Matsui"_s}},
        {u"medium"_s, u"manga"_s},
        {u"readingDirection"_s, u"RIGHT_TO_LEFT"_s},
        {u"status"_s, u"completed"_s},
        {u"readStatus"_s, u"IN_PROGRESS"_s},
        {u"declaredVolumes"_s, 21},
        {u"ownedVolumes"_s, 21},
        {u"missingVolumes"_s, QJsonArray{}},
        {u"missingChapters"_s, QJsonArray{3.5}},
        {u"genres"_s, QJsonArray{u"Action"_s, u"Comédie"_s}},
        {u"tags"_s, QJsonArray{u"École"_s}},
        {u"ageRating"_s, u"12+"_s},
        {u"publisher"_s, u"Kana"_s},
        {u"collection"_s, QJsonValue::Null},
        {u"colour"_s, true},
        {u"language"_s, u"fr"_s},
        {u"addedAt"_s, 1750000000},
        {u"lastAddedAt"_s, 1755000000},
    };
}

QJsonObject anEntry()
{
    return QJsonObject{
        {u"id"_s, u"en-12"_s},
        {u"type"_s, u"VOLUME"_s},
        {u"number"_s, 12},
        {u"title"_s, QJsonValue::Null},
        {u"pageCount"_s, 190},
        {u"chapterCount"_s, 4},
        {u"file"_s, u"Tome 12.cbz"_s},
        {u"size"_s, 61234567},
    };
}

} // namespace

class ReadsTheContract : public QObject
{
    Q_OBJECT

private slots:
    void anyScriptSurvives();
    void a_whole_series_arrives_intact()
    {
        const Api::Read<Api::Series> got = Api::series(aSeries());
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        const Api::Series &one = *got.value;

        QCOMPARE(one.id, u"ed-1"_s);
        QCOMPARE(one.name, u"Assassination Classroom"_s);
        QCOMPARE(one.entryCount, 21);
        QCOMPARE(one.chapterCount, 180);
        QCOMPARE(one.holding.ownedVolumes, 21);
        QCOMPARE(one.declaredVolumes, std::optional<int>(21));
        QCOMPARE(one.author, std::optional<QString>(u"Yūsei Matsui"_s));
        QCOMPARE(one.authors, QList<QString>({u"Yūsei Matsui"_s}));
        QCOMPARE(one.artists, QList<QString>({u"Yūsei Matsui"_s}));
        QCOMPARE(one.medium, std::optional<Api::Medium>(Api::Medium::Manga));
        QCOMPARE(one.readingDirection,
                 std::optional<Api::ReadingDirection>(Api::ReadingDirection::RightToLeft));
        QCOMPARE(one.run, std::optional<Api::Run>(Api::Run::Completed));
        QCOMPARE(one.holding.readStatus, std::optional<Api::ReadStatus>(Api::ReadStatus::InProgress));
        QCOMPARE(one.genres, QList<QString>({u"Action"_s, u"Comédie"_s}));
        QCOMPARE(one.tags, QList<QString>({u"École"_s}));
        QCOMPARE(one.ageRating, std::optional<QString>(u"12+"_s));
        QVERIFY(!one.collection.has_value());
        QCOMPARE(one.colour, std::optional<bool>(true));
        QCOMPARE(one.holding.missingChapters, QList<double>({3.5}));
        QVERIFY(one.holding.missingVolumes.isEmpty());
        QCOMPARE(one.holding.lastAddedAt, std::optional<qint64>(1755000000));
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
        without.remove(u"name"_s);

        const Api::Read<Api::Series> got = Api::series(without);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"name"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"missing"_s), qPrintable(got.trouble));
    }

    void a_required_field_set_to_null_refuses_too()
    {
        QJsonObject nulled = aSeries();
        nulled[u"work"_s] = QJsonValue::Null;

        const Api::Read<Api::Series> got = Api::series(nulled);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"work"_s), qPrintable(got.trouble));
    }

    /// The other half of the rule: the server may learn a word before this client does, and a
    /// shelf that emptied itself over one would be worse than a shelf with one odd tile.
    void a_word_the_client_never_heard_keeps_the_row()
    {
        QJsonObject odd = aSeries();
        odd[u"medium"_s] = u"lianhuanhua"_s;

        const Api::Read<Api::Series> got = Api::series(odd);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->medium, std::optional<Api::Medium>(Api::Medium::Other));
        QCOMPARE(got.value->name, u"Assassination Classroom"_s);
    }

    /// `medium` has a bucket for what this client has not been taught — `Other` — and these
    /// two have none. A word outside the three became Unread, and a word outside the two
    /// became Ongoing: a series that had stopped, shown as still running, and a collection
    /// read to the end, shown as untouched. Nothing is a fact this client can hold; a wrong
    /// value is not.
    void a_word_the_client_never_heard_is_absent_and_never_the_wrong_one()
    {
        QJsonObject odd = aSeries();
        odd[u"status"_s] = u"hiatus"_s;
        odd[u"readStatus"_s] = u"ARCHIVED"_s;

        const Api::Read<Api::Series> got = Api::series(odd);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->run, std::optional<Api::Run>());
        QCOMPARE(got.value->holding.readStatus, std::optional<Api::ReadStatus>());
        QCOMPARE(got.value->name, u"Assassination Classroom"_s);
    }

    void a_page_says_where_the_broken_item_is()
    {
        QJsonObject broken = aSeries();
        broken.remove(u"arcCount"_s);

        const QJsonObject body{
            {u"total"_s, 3},
            {u"page"_s, 0},
            {u"size"_s, 100},
            {u"items"_s, QJsonArray{aSeries(), broken, aSeries()}},
        };

        const Api::Read<Api::Page> got = Api::page(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"items[1]"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"arcCount"_s), qPrintable(got.trouble));
    }

    void a_good_page_keeps_its_counts()
    {
        const QJsonObject body{
            {u"total"_s, 337},
            {u"page"_s, 2},
            {u"size"_s, 100},
            {u"items"_s, QJsonArray{aSeries(), aSeries()}},
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
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Assassination Classroom"_s},
            {u"reason"_s, u"IN_PROGRESS"_s},
            {u"entry"_s, anEntry()},
            {u"progress"_s,
             QJsonObject{
                 {u"entryId"_s, u"en-12"_s},
                 {u"page"_s, 47},
                 {u"pageCount"_s, 190},
                 {u"finished"_s, false},
                 {u"updatedAt"_s, 1755000000},
                 {u"chapter"_s, QJsonObject{{u"id"_s, u"ch-98"_s},
                                            {u"raw"_s, u"098"_s},
                                            {u"label"_s, u"Chapitre 98"_s},
                                            {u"kind"_s, u"CHAPTER"_s},
                                            {u"position"_s, 2},
                                            {u"entryId"_s, u"en-12"_s}}},
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
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Berserk"_s},
            {u"reason"_s, u"NEXT_UP"_s},
            {u"entry"_s, anEntry()},
            {u"progress"_s, QJsonValue::Null},
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
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Berserk"_s},
            {u"reason"_s, u"NEXT_UP"_s},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"entry"_s), qPrintable(got.trouble));
    }

    /// The panel exists to show what you own, so a facet with no count is not a facet.
    void facets_keep_their_counts_and_drop_what_has_none()
    {
        const QJsonObject body{
            {u"media"_s, QJsonArray{QJsonObject{{u"value"_s, u"manga"_s}, {u"count"_s, 41}},
                                    QJsonObject{{u"value"_s, u"bd"_s}, {u"count"_s, 12}},
                                    QJsonObject{{u"value"_s, u"comics"_s}}}},
            {u"readStatuses"_s,
             QJsonArray{QJsonObject{{u"value"_s, u"UNREAD"_s}, {u"count"_s, 12}}}},
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

        // Asked before it is opened: a rename in `Api.cpp` that stopped one of these being
        // recognised would make this a dereference of an empty optional, and ctest would
        // report a segfault where it should report which word came back as nothing.
        for (const QString &word : {u"UNREAD"_s, u"IN_PROGRESS"_s, u"READ"_s}) {
            const std::optional<Api::ReadStatus> read = Api::readStatus(word);
            QVERIFY2(read.has_value(), qPrintable(word));
            QCOMPARE(Api::spell(*read), word);
        }
    }
};

// Any script, from the start.
//
// A library is not French. A work is named in the language it was published in, and the
// client has no say in which: Japanese, Chinese, Arabic, Cyrillic, or a Latin name with a
// macron on it. This asserts that what the server sends is what the client holds — the
// reason every literal here is UTF-16 rather than judged ASCII one at a time.
void ReadsTheContract::anyScriptSurvives()
{
    const QList<QString> names = {
        u"ハイキュー!!"_s,          // Japanese
        u"进击的巨人"_s,             // Chinese
        u"هجوم العمالقة"_s,          // Arabic, right to left
        u"Атака титанов"_s,         // Cyrillic
        u"Haikyū — l'été"_s,        // Latin, macron and an apostrophe
    };

    for (const QString &name : names) {
        QJsonObject one = aSeries();
        one[u"name"_s] = name;
        one[u"work"_s] = name;

        const Api::Read<Api::Series> read = Api::series(one);
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->name, name);
        QCOMPARE(read.value->work, name);
    }
}

QTEST_APPLESS_MAIN(ReadsTheContract)
#include "reads_the_contract.moc"
