// The three things French asks for that are invisible when present and obvious when missing.
//
// A test rather than care, because care does not survive the twentieth label. The non-breaking
// space in particular cannot be seen in a diff, cannot be seen in the source, and can only be
// seen on screen once the window is narrow enough to break the line in front of the colon.

#include "Words.h"

#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

class WritesFrench : public QObject
{
    Q_OBJECT

private slots:
    /// A BD comes in albums. That is the shelf's whole reason for reading the medium.
    void a_bd_comes_in_albums_and_everything_else_in_volumes()
    {
        QCOMPARE(Words::volumes(21, Api::Medium::Manga), u"21 tomes"_s);
        QCOMPARE(Words::volumes(7, Api::Medium::Bd), u"7 albums"_s);
        QCOMPARE(Words::volumes(11, Api::Medium::Comics), u"11 tomes"_s);
        QCOMPARE(Words::volumes(8, std::nullopt), u"8 tomes"_s);
    }

    /// French keeps the singular at zero as well as at one, which English does not.
    void zero_and_one_are_both_singular()
    {
        QCOMPARE(Words::volumes(1, Api::Medium::Manga), u"1 tome"_s);
        QCOMPARE(Words::volumes(0, Api::Medium::Manga), u"0 tome"_s);
        QCOMPARE(Words::volumes(1, Api::Medium::Bd), u"1 album"_s);
        QCOMPARE(Words::volumes(2, Api::Medium::Manga), u"2 tomes"_s);
    }

    /// « BD », never « Bd ». An acronym is why none of this can be a capitalise-the-first
    /// -letter helper run over the contract's spelling.
    void an_acronym_keeps_its_case()
    {
        QCOMPARE(Words::medium(Api::Medium::Bd), u"BD"_s);
        QCOMPARE(Words::medium(Api::Medium::Manga), u"Manga"_s);
        QCOMPARE(Words::medium(Api::Medium::Comics), u"Comics"_s);
    }

    /// One capital, at the start. « Non Lues » is the habit that arrives with every interface
    /// copied from an American one.
    void a_label_carries_exactly_one_capital()
    {
        for (const QString &label : {Words::readStatus(Api::ReadStatus::Unread),
                                     Words::readStatus(Api::ReadStatus::InProgress),
                                     Words::readStatus(Api::ReadStatus::Read)}) {
            QVERIFY2(!label.isEmpty(), "a status with no wording");
            QVERIFY2(label.at(0).isUpper(), qPrintable(label));
            const QString rest = label.mid(1);
            QVERIFY2(rest == rest.toLower(), qPrintable(label));
        }
        QCOMPARE(Words::readStatus(Api::ReadStatus::Unread), u"Non lues"_s);
    }

    /// The one that cannot be seen anywhere but on screen, and only once the line breaks.
    void a_colon_is_preceded_by_a_space_that_will_not_break()
    {
        const QString sorted = Words::labelled(u"Trier"_s, u"Nom"_s);
        QCOMPARE(sorted, u"Trier : Nom"_s);
        QVERIFY2(!sorted.contains(u" :"_s), "a breaking space before the colon");
        QVERIFY(sorted.contains(Words::Nbsp));
    }

    void the_band_reads_its_three_facts_in_order()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 12.0, 47, 190, u"Chapitre 98"_s),
                 u"Tome 12 · Page 47/190 · Chapitre 98"_s);
    }

    /// A chapter entry has already said which chapter it is. Saying it twice reads as two
    /// different chapters.
    void a_chapter_entry_does_not_name_its_chapter_twice()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Chapter, 98.0, 47, 190, u"Chapitre 98"_s),
                 u"Chapitre 98 · Page 47/190"_s);
    }

    /// Nothing started, nothing to say about a page. A card reading "Page /" is worse than a
    /// shorter card.
    void a_segment_with_nothing_to_say_is_left_out()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 1.0, std::nullopt, 190, std::nullopt),
                 u"Tome 1"_s);
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, std::nullopt, 47, 190, std::nullopt),
                 u"Page 47/190"_s);
    }

    /// A half is a side story, and French writes it with a comma. A whole one carries no
    /// decimal at all — « Tome 12,0 » is neither language.
    void a_half_volume_is_written_with_a_comma()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 3.5, std::nullopt, 0, std::nullopt),
                 u"Tome 3,5"_s);
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 12.0, std::nullopt, 0, std::nullopt),
                 u"Tome 12"_s);
    }

    /// The sentence that stops the shelf looking broken when a lit pill hides everything the
    /// search found. It names the pills exactly as the bar words them.
    void a_search_behind_a_lit_pill_says_so()
    {
        QCOMPARE(Words::nothingHere({u"Manga"_s}, 3),
                 u"Aucun résultat dans Manga · 3 sans les filtres"_s);
        QCOMPARE(Words::nothingHere({u"BD"_s, u"Non lues"_s}, 12),
                 u"Aucun résultat dans BD, Non lues · 12 sans les filtres"_s);
    }

    /// A straight apostrophe is the third of the three, and the easiest to type by accident.
    /// Nothing produced here has one today; this is what keeps it that way.
    void nothing_carries_a_straight_apostrophe()
    {
        QStringList every{Words::labelled(u"Trier"_s, u"Nom"_s), Words::nothingHere({u"BD"_s}, 1)};
        for (int i = 0; i <= int(Api::Medium::Other); ++i)
            every << Words::medium(Api::Medium(i));
        for (int i = 0; i <= int(Api::ReadStatus::Read); ++i)
            every << Words::readStatus(Api::ReadStatus(i));
        for (int i = 0; i <= int(Navigation::Destination::Settings); ++i)
            every << Words::destination(Navigation::Destination(i));
        for (int i = 0; i <= int(Widths::Band::Wide); ++i)
            every << Words::band(Widths::Band(i));

        for (const QString &one : std::as_const(every))
            QVERIFY2(!one.contains(u'\''), qPrintable(u"straight apostrophe in: "_s + one));
    }

    /// The placeholder card's own name for each screen — the five strings first written
    /// straight into `Main.qml`, where nothing tests them. `Navigation::label` returns exactly
    /// this, so this is what a QML change to that property would actually be checked against.
    void each_destination_names_itself()
    {
        QCOMPARE(Words::destination(Navigation::Destination::Shelf), u"Étagère"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Series), u"Série"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Reader), u"Lecteur"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Health), u"Santé"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Settings), u"Réglages"_s);
    }

    /// The card's second line — one label per band, the same three `Widths::bandFor` names.
    void each_band_is_named_in_french()
    {
        QCOMPARE(Words::band(Widths::Band::Wide), u"Large"_s);
        QCOMPARE(Words::band(Widths::Band::Medium), u"Moyenne"_s);
        QCOMPARE(Words::band(Widths::Band::Narrow), u"Étroite"_s);
    }

    /// « Étagère » and « Étroite » are the two of these eight new words that open on an
    /// accented capital. Nothing else here guards that: a straight "E" compiles, links, and
    /// is wrong to nobody but a reader looking at the actual screen. Pinned to the code point
    /// itself, not to another literal, since a copy-pasted "E" would match a wrong literal
    /// just as happily as the right one.
    void the_capitals_that_take_an_accent_keep_it()
    {
        QCOMPARE(Words::destination(Navigation::Destination::Shelf).at(0).unicode(),
                 char16_t(0x00C9)); // É
        QCOMPARE(Words::band(Widths::Band::Narrow).at(0).unicode(), char16_t(0x00C9)); // É
    }
};

QTEST_APPLESS_MAIN(WritesFrench)
#include "writes_french.moc"
