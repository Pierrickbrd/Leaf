#include "Isbn.h"

#include <QClipboard>
#include <QGuiApplication>

#include <array>

using namespace Qt::StringLiterals;

namespace {

// The three sets of EAN-13 patterns, seven modules per digit. Written out rather than derived
// from one another: a test derives them its own way and compares, which a derivation shared
// by both would not catch going wrong.
using Patterns = std::array<const char16_t *, 10>;
constexpr Patterns Left = {u"0001101", u"0011001", u"0010011", u"0111101", u"0100011",
                           u"0110001", u"0101111", u"0111011", u"0110111", u"0001011"};
constexpr Patterns Even = {u"0100111", u"0110011", u"0011011", u"0100001", u"0011101",
                           u"0111001", u"0000101", u"0010001", u"0001001", u"0010111"};
constexpr Patterns Right = {u"1110010", u"1100110", u"1101100", u"1000010", u"1011100",
                            u"1001110", u"1010000", u"1000100", u"1001000", u"1110100"};
// The first digit is not drawn: it is which of the left six take the even set.
constexpr Patterns Parity = {u"LLLLLL", u"LLGLGG", u"LLGGLG", u"LLGGGL", u"LGLLGG",
                             u"LGGLLG", u"LGGGLL", u"LGLGLG", u"LGLGGL", u"LGGLGL"};

bool isGuard(int module)
{
    return module < 3 || (module >= 45 && module < 50) || module >= 92;
}

bool allDigits(QStringView text)
{
    for (const QChar c : text) {
        if (c < u'0' || c > u'9')
            return false;
    }
    return true;
}

int digitAt(QStringView text, qsizetype i)
{
    return text.at(i).unicode() - u'0';
}

/// The EAN check digit of twelve digits: weights 1 and 3 alternating from the left.
QChar eanCheck(QStringView twelve)
{
    int sum = 0;
    for (qsizetype i = 0; i < 12; ++i)
        sum += digitAt(twelve, i) * (i % 2 == 0 ? 1 : 3);
    return QChar(u'0' + (10 - sum % 10) % 10);
}

/// An ISBN-10 adds up when the sum of each digit times its weight, ten down to one, is a
/// multiple of eleven; ten is written `X`.
bool isbn10Holds(QStringView ten)
{
    int sum = 0;
    for (qsizetype i = 0; i < 10; ++i) {
        const QChar c = ten.at(i);
        const int value = (i == 9 && (c == u'X' || c == u'x')) ? 10 : digitAt(ten, i);
        sum += value * int(10 - i);
    }
    return sum % 11 == 0;
}

/// The groups as the file wrote them, when it wrote the right number of them. Spaces count as
/// separators too: « 978 2 505 06533 3 » is the same hyphenation typed differently.
QStringList groupsOf(const QString &written)
{
    QStringList groups;
    QString one;
    for (const QChar c : written.trimmed()) {
        if (c == u'-' || c.isSpace()) {
            if (!one.isEmpty())
                groups << one;
            one.clear();
        } else {
            one += c;
        }
    }
    if (!one.isEmpty())
        groups << one;
    return groups;
}

} // namespace

Isbn::Isbn(QObject *parent)
    : QObject(parent)
{
}

std::optional<Isbn::Code> Isbn::read(const QString &written)
{
    const QStringList groups = groupsOf(written);
    const QString bare = groups.join(QString());

    QString digits;
    QStringList lines;
    if (bare.size() == 13) {
        if (!allDigits(bare) || eanCheck(QStringView(bare).first(12)) != bare.at(12))
            return std::nullopt;
        // An EAN outside the « Bookland » prefixes is a product code, not a book's number:
        // drawn under « ISBN », it would name something no bookseller can find (ruling of
        // 22/10/2026).
        if (!bare.startsWith(u"978"_s) && !bare.startsWith(u"979"_s))
            return std::nullopt;
        digits = bare;
        if (groups.size() == 5)
            lines = {groups.mid(0, 3).join(u'-'), groups.mid(3).join(u'-')};
    } else if (bare.size() == 10) {
        if (!allDigits(QStringView(bare).first(9))
            || !(bare.at(9).isDigit() || bare.at(9) == u'X' || bare.at(9) == u'x')
            || !isbn10Holds(bare))
            return std::nullopt;
        // The same book under the EAN prefix every ISBN-10 became: « 978 », the nine digits,
        // and a check digit computed again, since the old one was a sum modulo eleven.
        const QString twelve = u"978"_s + bare.first(9);
        digits = twelve + eanCheck(twelve);
        // Its four groups gain the prefix and lose the old check digit for the new one.
        if (groups.size() == 4 && groups.at(3).size() == 1)
            lines = {u"978-%1-%2"_s.arg(groups.at(0), groups.at(1)),
                     u"%1-%2"_s.arg(groups.at(2), QString(digits.back()))};
    } else {
        return std::nullopt;
    }
    if (lines.isEmpty())
        lines = {u"%1 %2"_s.arg(digits.first(3), digits.mid(3, 4)),
                 u"%1 %2"_s.arg(digits.mid(7, 5), digits.mid(12))};

    const QString modules = modulesOf(digits);
    return Code{digits, modules, barsOf(modules), lines};
}

QString Isbn::modulesOf(const QString &digits)
{
    QString modules = u"101"_s;
    const QStringView parity(Parity.at(digitAt(digits, 0)));
    for (qsizetype i = 1; i < 7; ++i)
        modules += QStringView(parity.at(i - 1) == u'L' ? Left.at(digitAt(digits, i))
                                                         : Even.at(digitAt(digits, i)));
    modules += u"01010"_s;
    for (qsizetype i = 7; i < 13; ++i)
        modules += QStringView(Right.at(digitAt(digits, i)));
    modules += u"101"_s;
    return modules;
}

QList<Isbn::Bar> Isbn::barsOf(const QString &modules)
{
    QList<Bar> bars;
    const int count = int(modules.size());
    int i = 0;
    while (i < count) {
        if (modules.at(i) != u'1') {
            ++i;
            continue;
        }
        const bool guard = isGuard(i);
        int end = i;
        while (end < count && modules.at(end) == u'1' && isGuard(end) == guard)
            ++end;
        bars.append({i, end - i, guard});
        i = end;
    }
    return bars;
}

QVariantMap Isbn::barcode(const QString &written) const
{
    const std::optional<Code> code = read(written);
    if (!code)
        return {};
    QVariantList bars;
    bars.reserve(code->bars.size());
    for (const Bar &one : code->bars)
        bars.append(QVariantMap{{u"from"_s, one.from}, {u"width"_s, one.width},
                                {u"guard"_s, one.guard}});
    return {{u"digits"_s, code->digits}, {u"lines"_s, code->lines}, {u"bars"_s, bars}};
}

void Isbn::copy(const QString &text) const
{
    if (QClipboard *board = QGuiApplication::clipboard())
        board->setText(text);
}
