// ComicInfo.xml, read the way the server reads it.
//
// Every test writes the document it reads, because each exists for one thing that was once
// wrong on the real library: a title that lost the space beside an ampersand, a chapter name
// that overwrote the volume's title, a Number written with a comma.

#include "ComicInfo.h"

#include <QElapsedTimer>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

QByteArray comic(const QString &inside)
{
    return (u"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ComicInfo>"_s + inside + u"</ComicInfo>"_s)
        .toUtf8();
}

} // namespace

class ReadsComicInfo : public QObject {
    Q_OBJECT
private slots:
    void a_komga_volume_gives_its_own_fields_and_its_series_fields_apart()
    {
        const auto read = ComicInfo::read(comic(
            u"<Number>13</Number><Title>Le bleu</Title><Series>Blue Period</Series>"
            u"<Writer>Tsubasa Yamaguchi</Writer><Publisher>Pika</Publisher>"
            u"<Genre>Seinen, Slice of life</Genre><LanguageISO>fr</LanguageISO>"
            u"<Count>14</Count><Year>2023</Year><Month>4</Month>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->number, std::optional<double>(13));
        QCOMPARE(read->title, u"Le bleu"_s);
        QCOMPARE(read->series, u"Blue Period"_s);
        QCOMPARE(read->author, u"Tsubasa Yamaguchi"_s);
        QCOMPARE(read->publisher, u"Pika"_s);
        QCOMPARE(read->language, u"fr"_s);
        QCOMPARE(read->genres, (QStringList{u"Seinen"_s, u"Slice of life"_s}));
        QCOMPARE(read->publishedOn, u"2023-4"_s);
        QCOMPARE(read->volumeCount, std::optional<int>(14));
    }

    /// `Penciller` is the drawing, which `Writer` does not say: read as the author was, it is
    /// a second person, and a volume drawn by someone other than its writer lost him.
    void the_penciller_is_the_artist()
    {
        const auto read = ComicInfo::read(
            comic(u"<Writer>Istin</Writer><Penciller> Nesmo </Penciller><Penciller>Autre</Penciller>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->author, u"Istin"_s);
        QCOMPARE(read->artist, u"Nesmo"_s);
        const auto alone = ComicInfo::read(comic(u"<Penciller>Nesmo</Penciller>"_s));
        QVERIFY(alone.has_value());
        QVERIFY(alone->author.isEmpty());
        const auto writer = ComicInfo::read(comic(u"<Writer>Istin</Writer>"_s));
        QVERIFY(writer.has_value());
        QVERIFY(writer->artist.isEmpty());
    }

    void the_first_spelling_of_a_tag_wins()
    {
        const auto read = ComicInfo::read(comic(u"<Title>Premier</Title><Title>Second</Title>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->title, u"Premier"_s);
    }

    void an_empty_tag_is_not_a_value()
    {
        const auto read = ComicInfo::read(
            comic(u"<Title>  </Title><Count></Count><Series>X</Series>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->title.isEmpty());
        QVERIFY(!read->volumeCount.has_value());
    }

    void an_empty_first_tag_does_not_hold_the_place_of_the_second()
    {
        const auto read = ComicInfo::read(comic(u"<Title> </Title><Title>Vrai</Title>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->title, u"Vrai"_s);
    }

    void a_chapter_name_never_becomes_the_volume_title()
    {
        const auto read = ComicInfo::read(comic(
            u"<Chapters><Chapter><Name>Le premier trait</Name></Chapter>"
            u"<Chapter><Name>Le second</Name></Chapter></Chapters>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->title.isEmpty());
        QCOMPARE(read->chapters, (QStringList{u"Le premier trait"_s, u"Le second"_s}));
    }

    void an_entity_keeps_the_spaces_around_it()
    {
        const auto read = ComicInfo::read(
            comic(u"<Title>The Knuckle &amp; The Arrow</Title><Series>Death&#38;Strawberry</Series>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->title, u"The Knuckle & The Arrow"_s);
        QCOMPARE(read->series, u"Death&Strawberry"_s);
    }

    void a_number_written_with_a_comma_is_read()
    {
        const auto read = ComicInfo::read(comic(u"<Number>56,5</Number>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->number, std::optional<double>(56.5));
    }

    void only_right_to_left_says_a_direction()
    {
        const auto rtl = ComicInfo::read(comic(u"<Manga>YesAndRightToLeft</Manga>"_s));
        QVERIFY(rtl.has_value());
        QCOMPARE(rtl->readingDirection, u"RIGHT_TO_LEFT"_s);
        const auto no = ComicInfo::read(comic(u"<Manga>No</Manga><Series>X</Series>"_s));
        QVERIFY(no.has_value());
        QVERIFY(no->readingDirection.isEmpty());
        const auto yes = ComicInfo::read(comic(u"<Manga>Yes</Manga><Series>X</Series>"_s));
        QVERIFY(yes.has_value());
        QVERIFY(yes->readingDirection.isEmpty());
    }

    void a_nested_tag_is_not_a_field()
    {
        const auto read = ComicInfo::read(
            comic(u"<Pages><Page><Title>x</Title></Page></Pages><Series>X</Series>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->title.isEmpty());
    }

    void the_other_tags_are_read_by_their_exact_names()
    {
        const auto read = ComicInfo::read(comic(
            u"<GTIN>9782205000000</GTIN><Summary>Un résumé</Summary><StoryArc>Arc 1</StoryArc>"
            u"<Year>2020</Year><Month>1</Month><Day>2</Day><title>minuscule</title>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->isbn, u"9782205000000"_s);
        QCOMPARE(read->summary, u"Un résumé"_s);
        QCOMPARE(read->arc, u"Arc 1"_s);
        QCOMPARE(read->publishedOn, u"2020-1-2"_s);
        QVERIFY(read->title.isEmpty());
    }

    void a_day_without_a_year_dates_nothing()
    {
        const auto read = ComicInfo::read(comic(u"<Day>2</Day><Series>X</Series>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->publishedOn.isEmpty());
    }

    void a_document_that_is_not_xml_is_unreadable()
    {
        QVERIFY(!ComicInfo::read("this is not xml").has_value());
        QVERIFY(!ComicInfo::read(comic(u"<Title>x</Titel>"_s)).has_value());
    }

    void bytes_that_are_not_utf8_are_unreadable()
    {
        QByteArray bytes = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><ComicInfo><Title>caf";
        bytes.append(char(0xE9));
        bytes.append("</Title></ComicInfo>");
        QVERIFY(!ComicInfo::read(bytes).has_value());
    }

    void a_document_with_nothing_useful_is_nothing()
    {
        QVERIFY(!ComicInfo::read(comic(QString())).has_value());
        QVERIFY(!ComicInfo::read(comic(u"<Title> </Title>"_s)).has_value());
    }

    void a_dtd_that_expands_is_refused_without_expanding()
    {
        QString dtd = u"<!DOCTYPE ComicInfo [<!ENTITY a0 \"lol\">"_s;
        for (int i = 1; i <= 10; ++i) {
            const QString previous = u"&a%1;"_s.arg(i - 1);
            dtd += u"<!ENTITY a%1 \"%2\">"_s.arg(i).arg(previous.repeated(10));
        }
        dtd += u"]>"_s;
        const QByteArray bomb =
            (u"<?xml version=\"1.0\"?>"_s + dtd + u"<ComicInfo><Title>&a10;</Title></ComicInfo>"_s)
                .toUtf8();
        QElapsedTimer clock;
        clock.start();
        QVERIFY(!ComicInfo::read(bomb).has_value());
        QVERIFY2(clock.elapsed() < 1000, "the DTD was expanded before being refused");
    }

    void a_harmless_dtd_is_refused_all_the_same()
    {
        const QByteArray xml = "<?xml version=\"1.0\"?><!DOCTYPE ComicInfo [<!ENTITY a \"x\">]>"
                               "<ComicInfo><Title>t</Title></ComicInfo>";
        QVERIFY(!ComicInfo::read(xml).has_value());
    }

    void a_cdata_section_is_not_a_value_as_on_the_server()
    {
        const auto read = ComicInfo::read(
            comic(u"<Title><![CDATA[x]]></Title><Series>S</Series>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->title.isEmpty());
    }

    void a_self_closing_child_empties_the_text_gathered_before_it()
    {
        // The server reads "S" here; Qt cannot tell <br/> from <br></br>.
        const auto read = ComicInfo::read(comic(u"<Series>S<br/></Series><Title>t</Title>"_s));
        QVERIFY(read.has_value());
        QVERIFY(read->series.isEmpty());
        QCOMPARE(read->title, u"t"_s);
    }

    void a_leading_byte_order_mark_is_ignored()
    {
        const auto read = ComicInfo::read("\xEF\xBB\xBF" + comic(u"<Title>t</Title>"_s));
        QVERIFY(read.has_value());
        QCOMPARE(read->title, u"t"_s);
    }

    void an_undeclared_entity_makes_the_file_unreadable()
    {
        QVERIFY(!ComicInfo::read(comic(u"<Title>a &nbsp; b</Title>"_s)).has_value());
    }
};

QTEST_APPLESS_MAIN(ReadsComicInfo)
#include "reads_comicinfo.moc"
