// The ISBN of a volume as the barcode on its back.
//
// The expected modules below were not produced by `Isbn`: they were derived on their own —
// the right-hand set as the left one inverted, the even set as the right one reversed — and
// written out, so that a pattern mistyped in the code's tables fails here instead of being
// copied into the expectation. A barcode that is wrong by one module still looks like a
// barcode; only a scanner, or this, notices.

#include "Isbn.h"

#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// « 978-2-505-06533-3 », the volume the panel was designed on (22/10/2026).
const QString Designed =
    u"10101110110001001001101101100010100111011000101010111001010100001001110100001010000101000010101"_s;
/// « 4006381333931 », an EAN whose first digit is not 9: its left half takes the parity
/// `LGLLGG`, which no ISBN does, so the table is tested beyond the one row books use.
const QString StartsWithFour =
    u"10100011010100111010111101111010001001011001101010100001010000101000010111010010000101100110101"_s;
/// « 0-8044-2957-X » as « 9780804429573 ».
const QString FromAnX =
    u"10101110110001001010011101101110100111010001101010101110011011001110100100111010001001000010101"_s;

} // namespace

class DrawsTheIsbn : public QObject
{
    Q_OBJECT

private slots:
    void an_isbn_13_gives_its_95_modules()
    {
        const auto code = Isbn::read(u"978-2-505-06533-3"_s);
        QVERIFY(code);
        QCOMPARE(code->digits, u"9782505065333"_s);
        QCOMPARE(code->modules.size(), 95);
        QCOMPARE(code->modules, Designed);
    }

    /// Drawn through `modulesOf`, since `read` refuses it: it is no ISBN.
    void the_first_digit_chooses_the_parity_of_the_left_half()
    {
        QCOMPARE(Isbn::modulesOf(u"4006381333931"_s), StartsWithFour);
    }

    /// A valid EAN-13 that does not begin with 978 or 979 is a product code, not an ISBN.
    void an_ean_outside_978_and_979_is_no_isbn()
    {
        QVERIFY(!Isbn::read(u"4006381333931"_s));
        QVERIFY(!Isbn::read(u"9771234567898"_s)); // a serial's ISSN, valid as an EAN
        QVERIFY(Isbn::read(u"979-10-90636-07-1"_s));
    }

    /// An ISBN-10 becomes the same book under « 978 », its check digit computed again: the old
    /// one was modulo eleven, and an `X` cannot be drawn.
    void an_isbn_10_with_an_x_becomes_its_isbn_13()
    {
        const auto code = Isbn::read(u"0-8044-2957-X"_s);
        QVERIFY(code);
        QCOMPARE(code->digits, u"9780804429573"_s);
        QCOMPARE(code->modules, FromAnX);
        QCOMPARE(code->lines, (QStringList{u"978-0-8044"_s, u"2957-3"_s}));
        QVERIFY(Isbn::read(u"080442957x"_s));
    }

    void a_wrong_check_digit_is_no_barcode()
    {
        // 978-2-505-06215 adds up to 8, not 9.
        QVERIFY(!Isbn::read(u"978-2-505-06215-9"_s));
        QVERIFY(Isbn::read(u"978-2-505-06215-8"_s));
        QVERIFY(!Isbn::read(u"0-8044-2957-1"_s));
    }

    void a_number_that_is_not_an_isbn_is_no_barcode()
    {
        QVERIFY(!Isbn::read(QString()));
        QVERIFY(!Isbn::read(u"   "_s));
        QVERIFY(!Isbn::read(u"<b>978</b>"_s));
        QVERIFY(!Isbn::read(u"978250506533"_s));     // twelve
        QVERIFY(!Isbn::read(u"97825050653333"_s));   // fourteen
        QVERIFY(!Isbn::read(u"978250506533X"_s));    // an X is ISBN-10's alone
        QVERIFY(!Isbn::read(u"X-8044-2957-0"_s));    // and only last
        QVERIFY(!Isbn::read(u"978-2-505-0653３-3"_s)); // a full-width digit is not a digit
    }

    /// The hyphens a file wrote are kept, split after the publisher; bare digits are grouped
    /// without hyphens, since where they go cannot be known without the agency's ranges.
    void the_number_is_shown_as_the_file_hyphenated_it_and_never_hyphenated_by_guess()
    {
        QCOMPARE(Isbn::read(u"978-2-505-06533-3"_s)->lines,
                 (QStringList{u"978-2-505"_s, u"06533-3"_s}));
        QCOMPARE(Isbn::read(u"978 2 505 06533 3"_s)->lines,
                 (QStringList{u"978-2-505"_s, u"06533-3"_s}));
        QCOMPARE(Isbn::read(u"9782505065333"_s)->lines,
                 (QStringList{u"978 2505"_s, u"06533 3"_s}));
        // Three groups is not a hyphenation, it is a number typed in pieces.
        QCOMPARE(Isbn::read(u"978-2505-065333"_s)->lines,
                 (QStringList{u"978 2505"_s, u"06533 3"_s}));
        QCOMPARE(Isbn::read(u"080442957X"_s)->lines,
                 (QStringList{u"978 0804"_s, u"42957 3"_s}));
    }

    /// Consecutive dark modules are one bar, and a guard bar never merges with a data bar.
    void the_bars_are_runs_of_modules_with_the_guards_apart()
    {
        const QList<Isbn::Bar> bars = Isbn::barsOf(Designed);
        // Rebuilt from the bars, the modules come back exactly.
        QString again(95, u'0');
        for (const Isbn::Bar &one : bars) {
            QVERIFY(one.width >= 1 && one.width <= 4);
            for (int i = one.from; i < one.from + one.width; ++i)
                again[i] = u'1';
        }
        QCOMPARE(again, Designed);
        // Two bars at each end, two in the middle, and nothing else long.
        QCOMPARE(std::count_if(bars.begin(), bars.end(), [](const Isbn::Bar &b) { return b.guard; }),
                 6);
        QCOMPARE(bars.first(), (Isbn::Bar{0, 1, true}));
        QCOMPARE(bars.last(), (Isbn::Bar{94, 1, true}));
        // EAN-13 always draws thirty bars: two per digit, and the six of the guards.
        QCOMPARE(bars.size(), 30);
    }

    void qml_reads_the_same_code_or_nothing()
    {
        Isbn isbn;
        QVERIFY(isbn.barcode(u"978-2-505-06215-9"_s).isEmpty());
        const QVariantMap code = isbn.barcode(u"978-2-505-06533-3"_s);
        QCOMPARE(code.value(u"digits"_s).toString(), u"9782505065333"_s);
        QCOMPARE(code.value(u"lines"_s).toStringList(),
                 (QStringList{u"978-2-505"_s, u"06533-3"_s}));
        QCOMPARE(code.value(u"bars"_s).toList().size(), 30);
        const QVariantMap first = code.value(u"bars"_s).toList().first().toMap();
        QCOMPARE(first.value(u"from"_s).toInt(), 0);
        QCOMPARE(first.value(u"width"_s).toInt(), 1);
        QVERIFY(first.value(u"guard"_s).toBool());
    }
};

QTEST_APPLESS_MAIN(DrawsTheIsbn)
#include "draws_the_isbn.moc"
